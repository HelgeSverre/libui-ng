# libui-ng fork — fix backlog

Verified-fixable bugs from an exhaustive triage of all open issues across `libui-ng/libui-ng` (101) and `andlabs/libui` (172) on 2026-06-23. Of 273 issues, ~190 were stale/obsolete/questions/meta/feature-requests; **18 were confirmed still-present and fixable** against current master (`43ba1ef5`).

> Note: #344 and #347 are the same Windows text-background fix; #294 and #184 are the same Windows DPI-awareness fix.

## ⚠️ Do not blindly cherry-pick these fork fixes

- **#411 (Windows negative-sweep arcs):** petabyt/kojix2 swap direction on the `negative` *flag* only — would invert correct positive sweeps. Use the sign-aware fix below.
- **#425 (macOS image transparency):** do NOT use kojix2's in-loop premultiply — it double-processes our straight-RGBA contract. Use the reporter's `NSAlphaNonpremultipliedBitmapFormat` one-liner.

---

## #294 — Controls and text pixelated on Windows at 200% scaling
*libui-ng/libui-ng · platforms: windows · impact: high · effort: L*

**Fork reference:** none (adapt petabyt/libui-dev extras/win_init.cpp SetProcessDpiAwarenessContext logic + PR #147 colorbutton SetTransform as references, but no listed fork-fix covers this)

Confirmed still present on master 43ba1ef (the exact commit our fork's third_party/libui-ng is pinned to). windows/init.cpp:74 has only the placeholder comment "// LONGTERM set DPI awareness" with no API call, and every windows/*.manifest lacks dpiAware/dpiAwareness keys. Process is therefore DPI-unaware -> DWM bitmap-stretches the window at 200% -> blurry controls/text. PR #147, which the maintainer points to, was CLOSED/never merged.

Fix (runtime-based, NOT manifest-based, because we ship a DLL and the manifest that governs DPI awareness is the HOST .exe's, not the DLL's):
1. third_party/libui-ng/windows/init.cpp — in uiInit(), before any window is created (right after initAlloc()), replace the LONGTERM comment with a runtime opt-in to Per-Monitor-V2, dynamically resolved so it still loads on Win7/8:
   - GetProcAddress(LoadLibraryW(L"User32.dll"), "SetProcessDpiAwarenessContext"); if present call it with DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4).
   - else GetProcAddress(LoadLibraryW(L"Shcore.dll"), "SetProcessDpiAwareness") -> PROCESS_PER_MONITOR_DPI_AWARE (2).
   - else fall back to SetProcessDPIAware() (Vista+). This mirrors petabyt/libui-dev extras/win_init.cpp.
2. Layout largely takes care of itself: windows/sizing.cpp:19-26 derives dialog base units from the actual UI font's GetTextMetricsW, so once hMessageFont is loaded at the monitor DPI the control geometry scales. The default message font comes from SystemParametersInfoW(SPI_GETNONCLIENTMETRICS) which returns DPI-correct metrics for the primary monitor once the process is aware, so a minimal first cut needs no font code change; for crisper per-monitor behavior, recreate hMessageFont sized to the monitor DPI (petabyt does CreateFont with -(dpiY/8) Segoe UI).
3. windows/colorbutton.cpp onWM_NOTIFY (around line 73-83) — the D2D DCRenderTarget defaults to 96 DPI, producing the off-center color-preview clipping cody271 flagged once aware. Add the PR #147 fix: rt->GetDpi(&dx,&dy); set a D2D1_MATRIX_3X2_F with m11=96/dx, m22=96/dy and rt->SetTransform before drawing. Audit other D2D-drawn / hardcoded-pixel controls (multilineentry minimum size, draw/area HDC paths) for similar 96-DPI assumptions.
4. Optionally also add the dpiAware/dpiAwareness keys to the bundled example/test manifests so the shipped demos are crisp, but the runtime call in (1) is the load-bearing fix for FFI consumers.

forkFixExists = none: petabyt's win_init.cpp is reference code only (not in our listed fork-fix set); PR #147 is unmerged. This must be authored fresh in our fork.

---

## #196 — Bug when disabling windows and getting entries values on macOS
*andlabs/libui · platforms: macos · impact: medium · effort: S*

**Fork reference:** none — no fork commit addresses this; new one-line read-side fix in darwin/entry.m uiEntryText

Real Cocoa quirk, still present in current libui-ng master (43ba1ef) in darwin/entry.m. The code path is unchanged from andlabs: uiEntryText (entry.m:121-124) reads `[e->textfield stringValue]`, and disabling goes through the default macro `uiDarwinControlDefaultSyncEnableState` (ui_darwin.h:103-110) which does `[textfield setEnabled:enabled]`. No fork fix exists for this.

Empirical verification on macOS 15.6 (compiled minimal Cocoa repros):
- Committed case (literal issue repro: type, click away to commit, then disable): stringValue is PRESERVED on modern macOS. This part of the original 2015 bug is already fixed by AppKit.
- Focused/uncommitted case (text field is still the active first responder when disabled): `[tf setEnabled:NO]` then `[tf stringValue]` returns EMPTY (''). The underlying NSTextField field-editor quirk the classifier described is genuinely still present here.

Verified fix: forcing the cell to commit the field editor before the value is needed restores correctness in both directions. Tested `[[tf window] endEditingFor:tf]` (returns 'focused-text' instead of ''), and committing before setEnabled:NO also preserves it.

Concrete fix (read-side, smallest and most robust) in third_party/libui-ng/darwin/entry.m, uiEntryText:
  char *uiEntryText(uiEntry *e) {
      NSWindow *w = [e->textfield window];
      if (w != nil)
          [w endEditingFor:e->textfield];   // commit any live field-editor text into the cell
      return uiDarwinNSStringToText([e->textfield stringValue]);
  }
This guarantees uiEntryText always reflects pending edits regardless of focus/disable ordering, fixing both the original window-disable scenario and the focused-read scenario. endEditingFor: is a no-op when the field is not being edited, so it is safe for all callers. Optionally also commit on the disable side, but the read-side fix alone covers all reported symptoms.

---

## #207 — uiWindowSetChild(uiArea) does not fill the window on GTK+ (area sized 0,0)
*andlabs/libui · platforms: linux · impact: medium · effort: S*

**Fork reference:** None. No commit in the listed fork-fix inventory (kojix2 @dev, petabyt) addresses GTK uiWindowSetChild expand/fill. This is fresh work, but trivially modeled on the existing in-tree uiBoxAppend (unix/box.c) and uiprivNewChildWithBox (unix/child.c) expand/align/save-restore patterns.

Root cause confirmed in current libui-ng master (unix/window.c + unix/area.c). uiWindowSetChild() adds the child via uiUnixControlSetContainer() -> the uiUnixControlDefaultSetContainer macro, which only does gtk_container_add(childHolderContainer, widget). It NEVER sets the child widget's hexpand/vexpand/halign/valign. The childHolderWidget (a horizontal GtkBox) has expand+FILL set on the BOX itself, so the box fills the window — but for a child inside a GtkBox to fill, the CHILD needs hexpand=TRUE and halign=GTK_ALIGN_FILL. Most controls work because uiBoxAppend (unix/box.c) and uiprivNewChildWithBox (unix/child.c) explicitly set those expand/align props on their children. uiWindowSetChild does NOT (note the standing "// TODO save and restore expands and aligns" comment right above it). A non-scrolling uiArea is a GtkDrawingArea subclass: its areaWidget_get_preferred_width/height only override sizes for SCROLLING areas; for a non-scrolling area they chain up to GtkDrawingArea, which reports a ~0 natural size and hexpand=FALSE. Result: GtkBox allocates the area its 0,0 natural size and it never fills. Wrapping in a stretchy uiBox works because uiBoxAppend sets hexpand/vexpand/FILL — exactly the issue's documented workaround.

Fix (unix/window.c, in uiWindowSetChild): mirror uiprivNewChildWithBox/uiBoxAppend. After setting the child (the `if (w->child != NULL)` add branch), set on the child's GtkWidget handle: gtk_widget_set_hexpand(cw, TRUE); gtk_widget_set_halign(cw, GTK_ALIGN_FILL); gtk_widget_set_vexpand(cw, TRUE); gtk_widget_set_valign(cw, GTK_ALIGN_FILL); where cw = GTK_WIDGET(uiControlHandle(child)). To honor the existing TODO and avoid leaking these props onto the widget if it is later re-parented, save the old hexpand/halign/vexpand/valign before overriding and restore them in the removal branch (the `if (w->child != NULL)` detach branch) — store them in the uiWindow struct (four fields). This is the same save/restore pattern already used by uiprivChild and boxChild. Bounded to one file; no public API change.

---

## #284 — Some tweaks for long paths on Windows
*libui-ng/libui-ng · platforms: windows · impact: medium · effort: S*

**Fork reference:** none -- no existing fork fix. Verified kojix2/libui-ng@dev windows/stddialogs.cpp is byte-identical at the relevant call (still hr = result->GetDisplayName(SIGDN_FILESYSPATH, &wname) at line 62, no GetLongPathName). petabyt/libui-dev fork list contains no dialog path-normalization fix either. This is net-new work for our fork.

In windows/stddialogs.cpp -> commonItemDialog(), the path returned by result->GetDisplayName(SIGDN_FILESYSPATH, &wname) is passed straight to toUTF8(wname) with no normalization. This returns 8.3 short names (PROGRA~1) and a \\?\ prefix for paths over MAX_PATH. Fix between line 62 (GetDisplayName) and line 67 (name = toUTF8(wname)):

1) Expand short (8.3) names to long names via GetLongPathNameW. Use the standard two-call idiom: DWORD len = GetLongPathNameW(wname, NULL, 0); if len != 0, allocate WCHAR longbuf[len] (via uiprivAlloc), call GetLongPathNameW(wname, longbuf, len), and use longbuf as the source; on failure (len == 0) fall back to wname so a path is still returned. GetLongPathNameW also works past MAX_PATH when the input carries the \\?\ prefix (per matyalatte's comment on the issue), so this single call covers both reported symptoms.

2) Strip the long-path prefix before converting: if the (now-expanded) string begins with L"\\\\?\\UNC\\", rewrite it to L"\\\\" + the remainder; else if it begins with L"\\\\?\\", skip the 4-char prefix. Then name = toUTF8(stripped); free the longbuf with uiprivFree.

Keep the existing out:/CoTaskMemFree(wname) cleanup unchanged (wname still comes from CoTaskMemAlloc). Allocate the long-name buffer with uiprivAlloc and free with uiprivFree to match the codebase allocator. No change needed in uiOpenFile/uiOpenFolder/uiSaveFile -- they all route through commonItemDialog, so all three dialogs are fixed at once. The toUTF8 helper (windows/utf16.cpp:26) is unaffected.

Bounded, ~15-20 lines, Windows-only, no public ui.h API change. Recommend a manual repro check on a deep folder (>260 chars) and a path containing a directory with an 8.3 alias to confirm both PROGRA~1 expansion and \\?\ stripping.

---

## #298 — Form unalignment when using spinbox with a label
*libui-ng/libui-ng · platforms: macos · impact: medium · effort: S*

**Fork reference:** kojix2/libui-ng @dev commit 61ef3a7dc5 "darwin: fix form baseline alignment for spinboxes" (2026-04-04) implements EXACTLY this fix: it adds -viewForFirstBaselineLayout and -viewForLastBaselineLayout to libui_spinbox, both returning self->tf. This is a clean cherry-pick / 4-line manual port into darwin/spinbox.m. NOTE: this commit is NOT in the previously-listed kojix2 fork-fix set, so it is newly identified here. (kojix2's branch also carries a separate spinbox enable/disable change in commit 641a9b8e75 that introduces self->libui_enabled — the baseline fix itself does not depend on it, so port only the two baseline methods unless also adopting the enable/disable fix.)

Root cause (confirmed on master): In darwin/form.m -[formView append:c:stretchy:], each form row vertically positions its label by constraining the LABEL's NSLayoutAttributeBaseline equal to the CONTROL's NSLayoutAttributeBaseline (NSLayoutAttributeTop only for NSScrollView). Controls that are real NSControl subclasses (NSTextField for uiEntry, NSDatePicker for uiDateTimePicker) expose a proper text baseline, so they align fine. But darwin/spinbox.m's libui_spinbox is a PLAIN NSView subclass wrapping an NSTextField (tf) + NSStepper. A plain NSView has no meaningful baseline: AppKit defaults its baseline to the view's BOTTOM edge. So the form pins the label's text baseline to the bottom of the spinbox view, pushing the label down and clipping it (exactly the screenshot in the issue). This is purely a darwin/macOS bug; Windows/unix lay forms out differently and are unaffected.

Fix: make libui_spinbox report a real baseline by delegating baseline layout to its inner text field. Add to the @implementation libui_spinbox in darwin/spinbox.m:

  - (NSView *)viewForFirstBaselineLayout { return self->tf; }
  - (NSView *)viewForLastBaselineLayout  { return self->tf; }

With these overrides, NSLayoutAttributeBaseline on the spinbox container resolves to the NSTextField's text baseline, and the form's existing baseline constraint centers the "Salary" label correctly. No change to form.m is needed. Verified our base darwin/spinbox.m is the unmodified original (no baseline override present), so the bug is still present in our fork.

---

## #309 — Cannot disable a spinbox
*libui-ng/libui-ng · platforms: darwin · impact: medium · effort: S*

**Fork reference:** None. No listed fork (kojix2 @dev, petabyt) carries a spinbox enable/disable fix. Apply cody271's diff from the issue thread directly to darwin/spinbox.m.

Confirmed still present on master and in our fork copy at /Users/helge/code/php-gui/third_party/libui-ng/darwin/spinbox.m.

Root cause: spinbox.m uses uiDarwinControlAllDefaults(uiSpinbox, spinbox) (last line of the file). The default SyncEnableState macro (ui_darwin.h lines 103-110) does:
  if ([handlefield respondsToSelector:@selector(setEnabled:)]) [handlefield setEnabled:enabled];
Here handlefield is the libui_spinbox, an NSView subclass. NSView does NOT respond to setEnabled:, so the respondsToSelector: guard is false and the call is a no-op. The actual interactive children — self->tf (NSTextField) and self->stepper (NSStepper), which DO respond to setEnabled: — are never reached. Hence uiControlDisable()/uiControlEnable() have no visible effect on a spinbox. windows/unix are unaffected (handle IS the native enableable control), matching the issue comments.

Fix (cody271's diff in the issue thread, verbatim, in darwin/spinbox.m):
1) In the @interface libui_spinbox declaration block, add:
   @property (getter=isEnabled) BOOL enabled;
2) In the @implementation, before the closing @end (after controlTextDidChange:), add:
   - (BOOL)isEnabled { return [self->tf isEnabled] && [self->stepper isEnabled]; }
   - (void)setEnabled:(BOOL)e { [self->tf setEnabled:e]; [self->stepper setEnabled:e]; }

Adding setEnabled: makes libui_spinbox respond to the selector, so the existing default SyncEnableState forwards to both inner controls. No change to the AllDefaults usage needed. Apply to the fork file above; rebuild the macOS prebuilt dylib shipped by the PHP/FFI binding. Optionally add a smoke test calling uiControlDisable on a spinbox.

---

## #317 — uiRadioButtonsSetSelected segmentation fault when index is out of bounds on gtk3
*libui-ng/libui-ng · platforms: linux/windows/macos · impact: medium · effort: S*

**Fork reference:** None — no listed fork (kojix2 @dev, petabyt libui-dev) carries a uiRadioButtonsSetSelected bounds-check fix. This is net-new work for our fork.

Add a bounds check to uiRadioButtonsSetSelected on all three backends. The valid argument range is n == -1 (clear selection) or 0 <= n < buttonCount.

unix/radiobuttons.c — the crash site. The real button count is r->buttons->len - 1 (index 0 is the hidden grouping button; real buttons live at indices 1..len-1). Currently:
    void uiRadioButtonsSetSelected(uiRadioButtons *r, int n)
    {
        GtkToggleButton *tb = GTK_TOGGLE_BUTTON(g_ptr_array_index(r->buttons, n + 1));
        ...
    }
n=4 with 3 buttons -> g_ptr_array_index(r->buttons, 5) reads past the array (len==4) and GTK_TOGGLE_BUTTON dereferences garbage -> segfault. Fix: guard before indexing.
    void uiRadioButtonsSetSelected(uiRadioButtons *r, int n)
    {
        GtkToggleButton *tb;
        // index 0 is the hidden grouping button; real buttons are 1..len-1
        if (n < -1 || n >= (int) (r->buttons->len - 1)) {
            // out of range; ignore (or uiprivUserBug)
            return;
        }
        if (n == -1) {
            // select the hidden button to clear the visible selection
            tb = GTK_TOGGLE_BUTTON(g_ptr_array_index(r->buttons, 0));
        } else
            tb = GTK_TOGGLE_BUTTON(g_ptr_array_index(r->buttons, n + 1));
        r->changing = TRUE;
        gtk_toggle_button_set_active(tb, TRUE);
        r->changing = FALSE;
    }
(Note: the existing code already had no -1 handling either; selecting the hidden button at index 0 is how an empty selection is represented, mirroring uiRadioButtonsSelected which returns i-1 and the -1 hidden-button design in uiNewRadioButtons.)

windows/radiobuttons.cpp — uiRadioButtonsSetSelected does SendMessage((*(r->hwnds))[n], BM_SETCHECK, BST_CHECKED, 0) with no bounds check; std::vector::operator[] out of range is UB. Add:
    if (n < -1 || n >= (int) r->hwnds->size())
        return;
before the SendMessage that uses [n] (the m = uiRadioButtonsSelected(r) path is already safe since m is derived from the vector).

darwin/radiobuttons.m — uiRadioButtonsSetSelected sets r->selected = n unconditionally then calls [r->buttons objectAtIndex:n], which raises NSRangeException (crash) for n out of range. Add guard at top:
    if (n < -1 || n >= (int) [r->buttons count])
        return;
and only assign r->selected after the range is validated.

Mirror the same guard on all three for API parity. Effort is small; a shared validation in the common ui.c wrapper would be even cleaner but per-backend guards match existing code style.

---

## #425 — Image Table issues (transparency &amp; background)
*andlabs/libui · platforms: darwin · impact: medium · effort: S*

**Fork reference:** DO NOT cherry-pick kojix2/libui-ng @dev "uiImageAppend RGBA premultiply" verbatim. That fork takes the OTHER approach: it keeps bitmapFormat:0 and premultiplies R/G/B by alpha inside the loop (adds a premultiply() helper). For THIS binding that would be wrong: the consumer feeds straight RGBA (fromPng converts to straight), so kojix2's premultiply would double-process / corrupt the data relative to how this binding's buffer is interpreted, and it bundles unrelated null/INT_MAX guards. Use the reporter's one-line bitmapFormat:NSAlphaNonpremultipliedBitmapFormat change instead, which matches the straight-RGBA contract and stays consistent with the working Windows path. (If the maintainers later decide to standardize on premultiplied input across all platforms per ui.h's wording, kojix2's loop change plus matching premultiply on Windows would be the larger, contract-changing alternative — but that is bigger scope and not what fixes #425 minimally.)

File: third_party/libui-ng/darwin/image.m, in uiImageAppend(). The NSBitmapImageRep is created with `bitmapFormat:0`, which tells Cocoa the pixel data is ALPHA-PREMULTIPLIED. But the de-facto input contract for this binding is STRAIGHT (non-premultiplied) RGBA: ui.h's wording aside, the PHP consumer's Image::fromPng() (docs/GUIDE.md:775) explicitly "converts to straight (non-premultiplied) RGBA" and src/Image.php documents "RGBA order" with no premultiply step. The loop copies bytes straight through (no multiply). The format/flag mismatch makes Core Graphics composite transparent-edge pixels incorrectly, producing the green/black halos around transparent edges seen in tables and images on macOS only (Windows is fine because its WIC/AlphaBlend path handles straight RGBA correctly).\n\nConcrete change (one line):\n  -    bitmapFormat:0\n  +    bitmapFormat:NSAlphaNonpremultipliedBitmapFormat\nin the [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:...] call in darwin/image.m (~line 40). No pixel-loop change needed. This is the exact fix the reporter (mischnic) verified in issue #425.\n\nApply as a patch in third_party/libui-ng + a corresponding entry in patches/ so it survives a re-pull, then rebuild the macOS dylib. Add a small visual/example check using an emoji or transparent PNG in a table to confirm no halo.

---

## #444 — darwin/cocoa: adding uiTab to uiGrid causes a stack overflow
*andlabs/libui · platforms: macos · impact: medium · effort: S*

**Fork reference:** none — no fork commit addresses this. Checked kojix2/libui-ng @dev and petabyt/libui-dev: their darwin/grid.m commits only cover empty-cell-view leaks and signed/unsigned compares, not the establishOurConstraints reentrancy/stack overflow. This is genuinely new work.

CONFIRMED still present in current libui-ng master (local fork third_party/libui-ng/darwin/grid.m is byte-identical to master). Recursion chain verified end-to-end:

1. gridView -append: (grid.m:551) calls -establishOurConstraints.
2. -establishOurConstraints (grid.m:235) loops over children and at lines 513 and 519 calls uiDarwinControlSetHuggingPriority(child, ...) for EACH child. This is unique to grid — box.m/form.m set hugging priority only in append/delete, never from inside establishOurConstraints, so they don't recurse.
3. For a container child (uiTab), uiDarwinControlSetHuggingPriority dispatches to uiTabSetHuggingPriority (tab.m:210), which UNCONDITIONALLY calls uiDarwinNotifyEdgeHuggingChanged(tab) at tab.m:218. (Plain controls use the default macro in ui_darwin.h:140 which only sets the NSView property and does NOT notify — that's why only tab/box/group children trigger it.)
4. uiDarwinNotifyEdgeHuggingChanged (control.m:68) -> uiDarwinControlChildEdgeHuggingChanged(parent=grid) -> uiGridChildEdgeHuggingChanged (grid.m:723) -> [gridView establishOurConstraints] (grid.m:727).
5. Back to step 2 -> unbounded recursion -> stack overflow (matches reporter's lldb backtrace of ~7800 establishOurConstraints frames). Affects any container child of a grid: uiTab, uiBox, uiGroup, uiForm.

FIX (localized to darwin/grid.m, ~8 lines, reentrancy guard — the idiom libui already uses, cf. uiDarwinShouldStopSyncEnableState):
- Add an ivar to the gridView @interface block (grid.m:31-40): `BOOL establishing;` (initialize to NO in -initWithG:, grid.m:167-181).
- At the very top of -establishOurConstraints (grid.m:235, before `[self removeOurConstraints]`):
    if (self->establishing) return;
    self->establishing = YES;
  and wrap the body so the flag is always cleared, e.g. set `self->establishing = NO;` immediately before each return path and at the end (the method has 3 early returns at lines 256, 284, plus the normal end at 535). Cleanest: factor the body into a helper and use @try { ... } @finally { self->establishing = NO; }, OR simply set the flag NO at the function end AND before the two early `return;` statements (children==0 at 256, all-hidden at 284) since those return before any SetHuggingPriority call so an explicit reset there is just hygiene.
  Effect: the inner re-entrant establishOurConstraints triggered by SetHuggingPriority becomes a no-op while the outer pass is still running. This is correct because the outer pass is already (re)building ALL constraints and setting ALL children's hugging priorities; the notification only needs to rebuild constraints, which the in-flight outer pass already does. Matches the reporter's own proposed flag fix.

No regression risk for the normal (non-recursive) case: establishing starts NO, set during the pass, cleared after — external calls (append/insert/visibility-change) still run the full pass.

---

## #323 — Afterimages remain inside a Group immediately after the layout is changed on windows
*libui-ng/libui-ng · platforms: windows · impact: medium · effort: M*

**Fork reference:** none — no fork (kojix2/libui-ng @dev, petabyt/libui-dev) commit addresses Windows group/container relayout invalidation. This is new work. Apply to the fork's vendored windows/group.cpp (path: /Users/helge/code/php-gui/third_party/libui-ng/windows/group.cpp).

Root cause (confirmed on master @43ba1ef and in the fork's vendored third_party/libui-ng @43ba1ef, identical): the entire Windows relayout chain repaints purely via SetWindowPos inside uiWindowsEnsureMoveWindowDuringResize (windows/winpublic.cpp:26). There is NO explicit InvalidateRect/RedrawWindow anywhere in the relayout path — verified: zero Invalidate/Redraw matches in windows/group.cpp and groupRelayout()/boxRelayout() rely solely on SetWindowPos.

When a sibling is hidden via uiControlHide and a label shrinks (progress_bar hidden + progress_txt 2 lines -> 1 line in the repro), uiGroupChildVisibilityChanged -> uiWindowsControlMinimumSizeChanged -> uiGroupMinimumSizeChanged -> groupRelayout() moves/resizes the child container; the child box then boxRelayout()s and shifts its buttons. SetWindowPos invalidates only the parent region uncovered by the moving child, but the groupbox is a transparent BS_GROUPBOX and its inner container (windows/container.cpp) is deliberately transparent: container WM_ERASEBKGND returns 1 (no erase) and WM_PAINT only FillRect's ps.rcPaint with the inherited background brush. So the area a relocated/hidden control vacated inside the group is never invalidated/erased, leaving stale pixels — the reported afterimage of the button.

Concrete fix (bounded): force a full redraw of the group's client area (frame + transparent container + children) at the end of groupRelayout() in windows/group.cpp. After the existing uiWindowsEnsureMoveWindowDuringResize(...) call, add:
  RedrawWindow(g->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
RDW_ALLCHILDREN is required so the transparent container and its children repaint over the stale region (a plain InvalidateRect(g->hwnd, NULL, TRUE) would not reach children because the container suppresses erase). For full generality (the same class of stale-pixel bug can occur in any container on programmatic show/hide), the same line keyed on the container HWND can be added at the end of boxRelayout() in windows/box.cpp; the group-level fix alone resolves issue #323's exact repro. Guard cost is negligible (only fires on relayout, not per-frame). Should also be validated to not regress flicker on continuous window resize — if flicker appears, scope the redraw to fire only from the ChildVisibilityChanged path rather than every WM_WINDOWPOSCHANGED-driven relayout.

---

## #184 — Blurry window on Windows and HiDPI displays
*andlabs/libui · platforms: windows · impact: medium · effort: L*

**Fork reference:** None. No fork fix exists for DPI awareness. The kojix2/libui-ng @dev and petabyt/libui-dev commit lists touch tab/combobox/menu/form/area/date-picker/arc/bitmap behaviors but none address Windows DPI awareness, WM_DPICHANGED, or per-monitor scaling. This is genuinely new work, not a cherry-pick.

VERIFIED STILL PRESENT in current master (43ba1ef, == local fork base). Two hard evidence points: (1) windows/init.cpp line 74 is literally just the comment `// LONGTERM set DPI awareness` with NO awareness call (no SetProcessDPIAware / SetProcessDpiAwareness / SetProcessDpiAwarenessContext anywhere). This is actually WORSE than the classifier note claimed ("only system-DPI awareness") — current master sets NO awareness at all. (2) windows/libui.manifest contains NO <dpiAware>/<dpiAwareness> element (only the Common-Controls v6 dependency + Vista/7 supportedOS). With the process DPI-unaware (the Windows default), Windows DWM bitmap-stretches the whole window on HiDPI/fractional scaling, producing the blurry text shown in the issue (ssylvan confirmed it still reproduced at 125% on "latest"; code unchanged since). draw.cpp (lines 44/80) and d2dscratch.cpp (line 55) DO read DPI via GetDeviceCaps(LOGPIXELSX/Y) and rt->GetDpi, but those report 96 when the process is unaware, so drawing happens at logical res and gets stretched.\n\nKEY CONSUMER CONSTRAINT: DPI awareness is a PER-PROCESS setting. For a static EXE it goes in the app manifest, but the PHP/FFI binding loads libui.dll into php.exe — whose manifest we do NOT control. So a manifest edit in libui.manifest will NOT help the FFI consumer. The only workable path for this binding is a RUNTIME call in uiInit() before any window/DC is created.\n\nMINIMAL FIX (small, removes blur but not a complete DPI solution): in windows/init.cpp replace the line-74 comment with a runtime call. Because the build targets _WIN32_WINNT 0x0600 (winapi.hpp lines 22-26, Vista), the modern APIs are not in the SDK headers and MUST be loaded dynamically via GetProcAddress to keep the build compiling. Pseudocode in uiInit, right after GetStartupInfoW:\n  HMODULE u = LoadLibraryW(L\"user32.dll\");\n  auto pSetCtx = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT))GetProcAddress(u, \"SetProcessDpiAwarenessContext\"); // Win10 1703+\n  if (pSetCtx && pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) { /* done */ }\n  else { // fallback Win8.1: shcore!SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE); else legacy user32!SetProcessDPIAware(); }\nMust run before window creation and may be a no-op if php.ini/host already set awareness (awareness can't change once a window exists) — handle that gracefully.\n\nCOMPLETE FIX (the Large part, what makes effort=L and risky for prebuilt binaries): after enabling per-monitor-v2, all metrics must scale with the window's actual DPI instead of assuming 96 — sizing.cpp dlgUnits (BaseX/BaseY from GetTextMetrics, lines 25-26/40-62) need GetDpiForWindow scaling; window.cpp must handle WM_DPICHANGED to re-layout and use the suggested rect; area/draw paths must propagate live DPI; image/icon assets need per-DPI selection. This touches layout, draw, and event plumbing broadly — high regression risk for prebuilt cross-version DLLs, which is why it stays lower priority.

---

## #97 — uiWindowSetContentSize does not mask the uiWindowOnContentSizeChanged callback (unix)
*libui-ng/libui-ng · platforms: linux · impact: low · effort: S*

**Fork reference:** None. Neither kojix2/libui-ng @dev nor petabyt/libui-dev addresses this masking bug. This is new work — a ~2-line deletion in unix/window.c onSizeAllocate.

File: unix/window.c (our fork: third_party/libui-ng/unix/window.c, tracks master verbatim).

Root cause: `changingSize` masking is broken ONLY on unix because the flag lifecycle is shared between the setter AND the signal handler. `uiWindowSetContentSize` sets changingSize=TRUE, calls async gtk_window_resize, then pumps `while (gtk_events_pending()) uiMainStep(1)`, then sets changingSize=FALSE. But `onSizeAllocate` (the size-allocate handler) ALSO clears the flag itself:

    if (w->changingSize)
        w->changingSize = FALSE;   // lines 68-69

gtk_window_resize may emit MULTIPLE size-allocate events while the loop is pumping (a spurious/intermediate allocate followed by the final one — the "spurious size-allocates" the code comments and issue #96/#97 commenter describe). The FIRST allocate clears changingSize, so any SUBSEQUENT allocate within the same pumped loop passes the `if (!w->changingSize)` guard at line 64 and fires onContentSizeChanged during a programmatic resize. That is the exact unmasking the issue reports.

Fix (bounded, ~2 lines): DELETE lines 68-69 in onSizeAllocate (the in-handler `if (w->changingSize) w->changingSize = FALSE;`). Let uiWindowSetContentSize exclusively own the flag — it already clears it at line 234 after draining all pending events. This mirrors the Windows implementation (windows/window.cpp), which is correct precisely because its WM_SIZE handler only READS changingSize (lines 111-113) and never clears it; the setter owns it (lines 370/374). darwin is unaffected (setContentSize is synchronous, no event-loop pump).

CRITICAL care point (classifier-flagged cachedWidth interaction): KEEP the cache update at lines 61-66 unconditional — cachedWidth/cachedHeight must still be updated during a masked resize so a later real user resize to a different size is detected correctly. Only the callback (line 64-65) stays gated by !changingSize; only the redundant flag-clear (68-69) is removed. Do NOT move the cache update under the changingSize guard.

Optional symmetric hardening (out of scope for #97 but same class): the position path has the identical pattern — onConfigure clears changingPosition in-handler (mirror lines) while uiWindowSetPosition pumps the loop. Same removal would fix the analogous position-callback unmasking, but the issue is scoped to content size so the minimal fix is size-only.

Verification done: fetched unix/window.c, darwin/window.m, windows/window.cpp from libui-ng master; our fork's unix/window.c is byte-identical to master in the affected region. grep confirms changingSize is referenced in only those two sites, so removing the handler clear is safe.

---

## #332 — Windows progress bar minimum set to 237 — too wide
*libui-ng/libui-ng · platforms: windows · impact: low · effort: S*

**Fork reference:** None in our listed FORK FIXES. kojix2 has a standalone sample commit 83479602f7a0d2ebab20e336664452fdb8edf2c3 (changes 238->107) that can be adapted, but note our base value is 237 not 238; treat as new one-line work targeting 237->107.

In windows/progressbar.cpp line 12, change `#define pbarWidth 237` to `#define pbarWidth 107`. The value is in dialog units (converted to pixels via uiWindowsSizingDlgUnitsToPixels in uiProgressBarMinimumSize). MS UX layout guidance lists 107 as an acceptable progress-bar minimum width, matching the dialog-unit minimums used by sibling controls, so 237 forces an unnecessarily wide minimum size. Leave pbarHeight (8) unchanged. In our fork the file is at third_party/libui-ng/windows/progressbar.cpp — same line. After the source edit the prebuilt Windows libui.dll must be rebuilt to ship the change.

---

## #344 — windows bugs for drawText
*libui-ng/libui-ng · platforms: windows · impact: low · effort: S*

**Fork reference:** None. No fork fix in the inventory (kojix2/libui-ng @dev, petabyt/libui-dev) touches the Windows uiDrawText background-fill path; this is net-new work. The color-emoji half of #344 is separately already handled by our merged commits 958cbdf/892e6b7 (issue #1).

Implement the background-fill pass in uiDrawText() in windows/drawtext.cpp (our fork: third_party/libui-ng/windows/drawtext.cpp, ~lines 574-578; upstream master ~lines 482-486). The collected backgroundParams are never used: the loop is left as a commented-out `/* for (auto p : *(tl->backgroundParams)) { // TODO } */`, so uiNewBackgroundAttribute is a silent no-op on Windows (Cocoa/GTK ports implement it).

The infrastructure is already in place: windows/draw.hpp defines `struct drawTextBackgroundParams { size_t start; size_t end; double r,g,b,a; }`; attrstr.cpp addBackgroundParams() populates start/end as UTF-16 offsets (processAttribute converts UTF-8->UTF-16 before the switch) and pushes them into tl->backgroundParams; uiDrawNewTextLayout stores the vector and uiDrawFreeTextLayout frees it. Only the draw step is missing.

Concrete change inside uiDrawText(), before creating `black`/the textRenderer and drawing glyphs (so backgrounds sit behind text):

```cpp
for (auto p : *(tl->backgroundParams)) {
    DWRITE_HIT_TEST_METRICS *metrics = NULL;
    UINT32 count = 0;
    HRESULT hr;
    // first call to size the array (returns E_NOT_SUFFICIENT_BUFFER)
    hr = tl->layout->HitTestTextRange(
        (UINT32) p->start, (UINT32) (p->end - p->start),
        (FLOAT) x, (FLOAT) y,
        NULL, 0, &count);
    if (count == 0)
        continue;
    metrics = new DWRITE_HIT_TEST_METRICS[count];
    hr = tl->layout->HitTestTextRange(
        (UINT32) p->start, (UINT32) (p->end - p->start),
        (FLOAT) x, (FLOAT) y,
        metrics, count, &count);
    if (hr != S_OK) { delete[] metrics; logHRESULT(L"error hit-testing background range", hr); }
    ID2D1SolidColorBrush *bg = mustMakeSolidBrush(c->rt, p->r, p->g, p->b, p->a);
    for (UINT32 i = 0; i < count; i++) {
        D2D1_RECT_F rect;
        rect.left   = metrics[i].left;
        rect.top    = metrics[i].top;
        rect.right  = metrics[i].left + metrics[i].width;
        rect.bottom = metrics[i].top  + metrics[i].height;
        c->rt->FillRectangle(&rect, bg);
    }
    bg->Release();
    delete[] metrics;
}
```

Notes: HitTestTextRange already accepts the origin (x,y) and returns one DWRITE_HIT_TEST_METRICS rect per line the range spans (handles wrapping/multiline), with coordinates in the same DIP space as the subsequent layout->Draw(NULL, renderer, x, y) call, so no extra offset math is needed. start/end are already UTF-16 indices as required by HitTestTextRange. Then delete the now-obsolete commented-out TODO block. No public API/ABI change (ui.h unaffected); purely makes uiNewBackgroundAttribute functional on Windows.

---

## #347 — Text: BackgroundAttribute Windows (uiNewBackgroundAttribute does not render on Windows)
*andlabs/libui · platforms: windows · impact: low · effort: S*

**Fork reference:** none — no kojix2/dev or petabyt/libui-dev commit covers Windows background-attribute rendering; this is net-new fork work.

CONFIRMED still present in libui-ng master (43ba1ef5) and in the local fork at third_party/libui-ng/windows/drawtext.cpp. uiNewBackgroundAttribute params ARE collected (windows/attrstr.cpp addBackgroundParams, lines ~268-277, into struct drawTextBackgroundParams {size_t start,end; double r,g,b,a} defined in windows/attrstr.hpp lines 68-75), but uiDrawText() never paints them: the fill loop is a commented-out TODO at windows/drawtext.cpp lines 573-578 (master 482-486):\n  /*\n  for (auto p : *(tl->backgroundParams)) {\n      // TODO\n  }\n  */\nDarwin (darwin/drawtext.m line ~111 iterates backgroundParams and fills) and unix (unix/attrstr.c uses pango_attr_background_new) both render it, so Windows is the lone port dropping background color — a cross-platform parity bug.\n\nFIX (windows/drawtext.cpp, inside uiDrawText, BEFORE creating the textRenderer / drawing glyph runs so fills sit behind text): replace the commented block with a loop that, for each drawTextBackgroundParams p, converts the UTF-16 char range [p->start, p->end) into device rects via IDWriteTextLayout::HitTestTextRange and fills each rect with a solid brush of (p->r,p->g,p->b,p->a), offset by the layout origin (x,y). Sketch:\n\n  for (auto p : *(tl->backgroundParams)) {\n      UINT32 startPos = (UINT32) p->start;\n      UINT32 len = (UINT32) (p->end - p->start);\n      UINT32 nrects = 0;\n      // first call to size the array\n      tl->layout->HitTestTextRange(startPos, len, (FLOAT) x, (FLOAT) y,\n          NULL, 0, &nrects);\n      if (nrects == 0)\n          continue;\n      std::vector<DWRITE_HIT_TEST_METRICS> metrics(nrects);\n      hr = tl->layout->HitTestTextRange(startPos, len, (FLOAT) x, (FLOAT) y,\n          metrics.data(), nrects, &nrects);\n      if (hr != S_OK)\n          logHRESULT(L\"error hit-testing background range\", hr);\n      ID2D1SolidColorBrush *bg = mustMakeSolidBrush(c->rt, p->r, p->g, p->b, p->a);\n      for (UINT32 i = 0; i < nrects; i++) {\n          D2D1_RECT_F rect;\n          rect.left   = metrics[i].left;\n          rect.top    = metrics[i].top;\n          rect.right  = metrics[i].left + metrics[i].width;\n          rect.bottom = metrics[i].top  + metrics[i].height;\n          c->rt->FillRectangle(&rect, bg);\n      }\n      bg->Release();\n  }\n\nNotes: the existing mustMakeSolidBrush helper (drawtext.cpp ~129) and the textRenderer's FillRectangle pattern (used for underlines) prove the rendering primitives are available; only the range->rect mapping + fill loop is missing. HitTestTextRange's first call returns E_NOT_SUFFICIENT_BUFFER which is expected when sizing — treat nrects==0 as 'nothing to draw'. p->start/end are UTF-16 indices, matching IDWriteTextLayout's coordinate space, so no u16/u8 table conversion is needed. Bounded, self-contained change in one function of one file.

---

## #368 — DrawTextLayout alignment
*andlabs/libui · platforms: darwin · impact: low · effort: S*

**Fork reference:** none — no fork (kojix2 @dev or petabyt/libui-dev) lists a darwin drawtext alignment fix. This is new work: apply the bounded one-line guard above to third_party/libui-ng/darwin/drawtext.m.

CONFIRMED still present in current libui-ng master and in our fork checkout at third_party/libui-ng/darwin/drawtext.m (lines 70-83, in -[uiprivTextFrame initWithLayoutParams:]).

Root cause: the path rect that bounds the CTFrame is built from self->size, which is the size returned by CTFramesetterSuggestFrameSizeWithConstraints(). That "suggested" width collapses to the longest line's width, not the requested p->Width. Core Text DOES honor alignment (darwin/attrstr.m:433-447 maps uiDrawTextAlignLeft/Center/Right to kCTTextAlignmentLeft/Center/Right via kCTParagraphStyleSpecifierAlignment), but it aligns within the frame path rect. Because that rect is the narrow longest-line frame, center/right alignment have no extra horizontal space to align into, so they look identical to left alignment instead of aligning to p->Width.

Current code (drawtext.m ~lines 67-83):
  cgwidth = (CGFloat)(p->Width);
  if (cgwidth < 0) cgwidth = CGFLOAT_MAX;
  self->size = CTFramesetterSuggestFrameSizeWithConstraints(self->framesetter, range, NULL, CGSizeMake(cgwidth, CGFLOAT_MAX), &unused);
  rect.origin = CGPointZero;
  rect.size = self->size;                 // <-- bug: path width = longest line, not requested width
  self->path = CGPathCreateWithRect(rect, NULL);
  self->frame = CTFramesetterCreateFrame(self->framesetter, range, self->path, NULL);

Fix: when an explicit width was requested (p->Width >= 0), set the PATH width to the requested cgwidth so alignment has the full frame to work in. Do NOT use the reporter's unconditional `rect.size.width = cgwidth`, because for the unbounded case (p->Width < 0) cgwidth == CGFLOAT_MAX and that would make a CGFLOAT_MAX-wide path. Concrete change:

  rect.origin = CGPointZero;
  rect.size = self->size;
  if (p->Width >= 0)                       // explicit width: align within requested width
      rect.size.width = cgwidth;
  self->path = CGPathCreateWithRect(rect, NULL);

Leave self->size untouched so uiDrawTextLayoutExtents() (via returnWidth:) keeps reporting the natural text width, and the draw y-offset (uses self->size.height only) is unaffected. This is a single bounded change in one darwin file; unix (GTK/Pango) and windows (DirectWrite) lay out alignment differently and are not affected by this code path.

Verification suggestion: add a smoke test in the PHP/FFI binding drawing an area with uiDrawTextLayoutParams Align=Center/Right and Width wider than the text, asserting the rendered text shifts (compare pixel column or just visual/manual on macOS). No existing fork patch covers this (patches/ does not touch drawtext.m alignment).

---

## #411 — Arcs aren't drawn correctly on Windows 10, if sweep is negative
*andlabs/libui · platforms: windows · impact: low · effort: S*

**Fork reference:** petabyt/libui-dev commit ef0ce5280b5480b1dd6b78baf298c370d53c8b12 ("windows: Fixed arc drawing sweep direction to match Unix/Mac platforms"), identical to kojix2/libui-ng@dev commit 700167fe. BOTH forks apply only an unconditional swap of the two D2D1_SWEEP_DIRECTION_* constants keyed on a->negative, and do NOT touch arcSize. This is INCOMPLETE/likely-incorrect: it keys off the negative FLAG only, never the sign of the sweep VALUE, which is exactly what the issue#411 repro exercises (negative=0, sweep<0). By first-principles geometry it would also invert the positive-sweep case that the issue confirms already renders correctly. Recommendation: do NOT cherry-pick the fork commit verbatim; adapt it into the sign-aware version in fixSketch (XOR of negative-flag and sweep<0 for direction; fabs(sweep) for arcSize). The fork commit is useful as a pointer to the exact file/lines (windows/drawpath.cpp drawArc, the sweepDirection/arcSize block, ~lines 144-161 on master).

Root cause is in windows/drawpath.cpp -> static void drawArc(). The endpoints are computed correctly from startAngle and startAngle+sweep (so they are right even for negative sweep), but the Direct2D D2D1_ARC_SEGMENT.sweepDirection and arcSize are chosen ONLY from a->negative (the flag), never from the SIGN of a->sweep. The failing repro passes negative=0 and a negative sweep VALUE, so D2D draws the arc the wrong way around between the (correct) endpoints. macOS (CGPathAddArc with startAngle..startAngle+sweep) and Linux (cairo_arc / cairo_arc_negative) get this right because they are defined by start/end angles, not by an explicit visual direction + arcSize.

Correct, sign-aware fix (handles all 4 cases: positive/negative sweep x negative flag 0/1):
  // effective rotation direction = negative-flag XOR (sweep < 0)
  BOOL ccw = (a->negative != 0) ^ (a->sweep < 0);
  as.sweepDirection = ccw ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE
                          : D2D1_SWEEP_DIRECTION_CLOCKWISE;
  // arcSize must use the MAGNITUDE of the sweep, not the signed value:
  if (fabs(a->sweep) > uiPi)
      as.arcSize = D2D1_ARC_SIZE_LARGE;
  else
      as.arcSize = D2D1_ARC_SIZE_SMALL;

(Note: the existing arcSize block already inverts LARGE/SMALL when a->negative is set; replacing the whole if/else with the magnitude-based version above plus the XOR direction is cleaner and removes the two TODO comments.) Also worth touching the fullCircle branch (a->sweep = uiPi) to preserve sign for symmetry, though the half-circle split is direction-agnostic so it is not strictly required.

Geometric justification: render target is plain Y-down (windows/areadraw.cpp applies only an identity-scale scroll translate, no Y flip), so increasing parameter angle = visually clockwise = D2D1_SWEEP_DIRECTION_CLOCKWISE. Positive sweep with negative=0 must be CLOCKWISE (this is why the original code works for positive sweeps, matching the issue screenshots); negative sweep must flip to COUNTER_CLOCKWISE.

---

## #527 — Crashes in example 'test'
*andlabs/libui · platforms: windows/linux/macos · impact: low · effort: S*

**Fork reference:** None. No listed fork fix (kojix2/libui-ng @dev or petabyt/libui-dev) addresses uiFormDelete bounds checking. This is new work: a 3-line guard per backend in our HelgeSverre/libui-ng @maintained fork.

Root cause: uiFormDelete has no bounds check on `index`; deleting past the last element (or from an empty form) is out-of-bounds access. Reproduced by test page 13's "Delete First" button (calls uiFormDelete(f,0) repeatedly until the form is empty, then once more -> crash). Confirmed still present on current libui-ng master AND in our fork (HelgeSverre/libui-ng @maintained) on all three backends:

- windows/form.cpp:275 uiFormDelete: `fc = (*(f->controls))[index]; ... f->controls->erase(f->controls->begin() + index);` — std::vector::operator[] and erase with index >= size() are undefined behavior -> hard crash (the original Win7 32-bit report).
- unix/form.c:104 uiFormDelete: `fc = ctrl(f, index)` where `ctrl` is `#define ctrl(f,i) &g_array_index(f->children, struct formChild, i)` — g_array_index is unchecked; dereferencing fc->c on an out-of-range index is UB; g_array_remove_index then also trips a glib g_return_if_fail.
- darwin/form.m:398 -[... delete:] : `[self->children objectAtIndex:n]` on an empty/short NSMutableArray raises an uncaught NSRangeException -> process abort.

This is a public C-API bug (uiFormDelete in ui.h, doc comment specifies no bounds contract), so it is reachable from the PHP/FFI binding, not test-only. The sibling uiBoxDelete (windows/box.cpp:266 etc.) has the same unchecked pattern and should be hardened too.

Concrete fix — add an explicit bounds guard at the top of each uiFormDelete using the existing uiprivUserBug(...) macro (common/uipriv.h:35), which reports a clean diagnostic and aborts deterministically instead of UB/silent corruption, matching libui-ng's existing user-error reporting idiom:

windows/form.cpp, top of uiFormDelete:
  if (index < 0 || (size_t) index >= f->controls->size())
      uiprivUserBug(\"index %d out of range in uiFormDelete()\", index);

unix/form.c, top of uiFormDelete:
  if (index < 0 || (guint) index >= f->children->len)
      uiprivUserBug(\"index %d out of range in uiFormDelete()\", index);

darwin/form.m, top of -[... delete:] (or in uiFormDelete before calling it):
  if (n < 0 || (NSUInteger) n >= [self->children count])
      uiprivUserBug(\"index %d out of range in uiFormDelete()\", n);

(uipriv.h is already included via the per-platform ui*.h headers.) Optionally apply the same guard to uiFormAppend's index-using siblings and to uiBoxDelete for consistency. Update the ui.h doc comment for uiFormDelete to state that index must be in [0, uiFormNumChildren). On the PHP/FFI side, the binding's Form facade should also validate index against uiFormNumChildren() before calling, so consumers get a PHP exception rather than a native abort.

---

