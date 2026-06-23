// 6 april 2015
#include "uipriv_windows.hpp"
#include "attrstr.hpp"

HINSTANCE hInstance;
int nCmdShow;

HFONT hMessageFont;

// LONGTERM needed?
HBRUSH hollowBrush;

// the returned pointer is actually to the second character
// if the first character is - then free, otherwise don't
static const char *initerr(const char *message, const WCHAR *label, DWORD value)
{
	WCHAR *sysmsg;
	BOOL hassysmsg;
	WCHAR *wmessage;
	WCHAR *wout;
	char *out;

	hassysmsg = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, value, 0, (LPWSTR) (&sysmsg), 0, NULL) != 0;
	if (!hassysmsg)
		sysmsg = (WCHAR *) L"";			// TODO
	wmessage = toUTF16(message + 1);
	wout = strf(L"-error initializing libui: %s; code %I32d (0x%08I32X) %s",
		wmessage,
		value, value,
		sysmsg);
	uiprivFree(wmessage);
	if (hassysmsg)
		LocalFree(sysmsg);		// ignore error
	out = toUTF8(wout);
	uiprivFree(wout);
	return out + 1;
}

#define ieLastErr(msg) initerr("=" msg, L"GetLastError() ==", GetLastError())
#define ieHRESULT(msg, hr) initerr("=" msg, L"HRESULT", (DWORD) hr)

// LONGTERM put this declaration in a common file
uiInitOptions uiprivOptions;

#define wantedICCClasses ( \
	ICC_STANDARD_CLASSES |	/* user32.dll controls */		\
	ICC_PROGRESS_CLASS |		/* progress bars */			\
	ICC_TAB_CLASSES |			/* tabs */					\
	ICC_LISTVIEW_CLASSES |		/* table headers */			\
	ICC_UPDOWN_CLASS |		/* spinboxes */			\
	ICC_BAR_CLASSES |			/* trackbar */				\
	ICC_DATE_CLASSES |		/* date/time picker */		\
	0)

const char *uiInit(uiInitOptions *o)
{
	STARTUPINFOW si;
	const char *ce;
	HICON hDefaultIcon;
	HCURSOR hDefaultCursor;
	NONCLIENTMETRICSW ncm;
	INITCOMMONCONTROLSEX icc;
	HRESULT hr;

	if (o) {
		uiprivOptions = *o;
	}

	initAlloc();

	nCmdShow = SW_SHOWDEFAULT;
	GetStartupInfoW(&si);
	if ((si.dwFlags & STARTF_USESHOWWINDOW) != 0)
		nCmdShow = si.wShowWindow;

	// set DPI awareness so the process renders crisp on HiDPI displays
	// instead of being DWM bitmap-stretched (issues #294 / #184).
	// the modern APIs are not in the Vista-era SDK headers and may be
	// absent on older systems, so resolve them dynamically and fall back.
	// this must run before any window or device context is created;
	// if the host process already set awareness, the calls are no-ops.
	{
		HMODULE user32 = LoadLibraryW(L"user32.dll");
		BOOL awarenessSet = FALSE;

		if (user32 != NULL) {
			// Windows 10 1703+: per-monitor-aware v2.
			// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE) -4
			typedef BOOL (WINAPI *setProcessDpiAwarenessContextT)(HANDLE);
			setProcessDpiAwarenessContextT pSetProcessDpiAwarenessContext =
				(setProcessDpiAwarenessContextT) GetProcAddress(user32, "SetProcessDpiAwarenessContext");
			if (pSetProcessDpiAwarenessContext != NULL)
				if (pSetProcessDpiAwarenessContext((HANDLE) -4))
					awarenessSet = TRUE;
		}

		if (!awarenessSet) {
			// Windows 8.1+: per-monitor DPI awareness (shcore.dll).
			// PROCESS_PER_MONITOR_DPI_AWARE == 2
			HMODULE shcore = LoadLibraryW(L"shcore.dll");
			if (shcore != NULL) {
				typedef HRESULT (WINAPI *setProcessDpiAwarenessT)(int);
				setProcessDpiAwarenessT pSetProcessDpiAwareness =
					(setProcessDpiAwarenessT) GetProcAddress(shcore, "SetProcessDpiAwareness");
				if (pSetProcessDpiAwareness != NULL)
					if (SUCCEEDED(pSetProcessDpiAwareness(2)))
						awarenessSet = TRUE;
				FreeLibrary(shcore);
			}
		}

		if (!awarenessSet && user32 != NULL) {
			// Vista+: system DPI awareness.
			typedef BOOL (WINAPI *setProcessDPIAwareT)(void);
			setProcessDPIAwareT pSetProcessDPIAware =
				(setProcessDPIAwareT) GetProcAddress(user32, "SetProcessDPIAware");
			if (pSetProcessDPIAware != NULL)
				pSetProcessDPIAware();
		}

		if (user32 != NULL)
			FreeLibrary(user32);
	}

	hDefaultIcon = LoadIconW(NULL, IDI_APPLICATION);
	if (hDefaultIcon == NULL)
		return ieLastErr("loading default icon for window classes");
	hDefaultCursor = LoadCursorW(NULL, IDC_ARROW);
	if (hDefaultCursor == NULL)
		return ieLastErr("loading default cursor for window classes");

	ce = initUtilWindow(hDefaultIcon, hDefaultCursor);
	if (ce != NULL)
		return initerr(ce, L"GetLastError() ==", GetLastError());

	if (registerWindowClass(hDefaultIcon, hDefaultCursor) == 0)
		return ieLastErr("registering uiWindow window class");

	ZeroMemory(&ncm, sizeof (NONCLIENTMETRICSW));
	ncm.cbSize = sizeof (NONCLIENTMETRICSW);
	if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof (NONCLIENTMETRICSW), &ncm, sizeof (NONCLIENTMETRICSW)) == 0)
		return ieLastErr("getting default fonts");
	hMessageFont = CreateFontIndirectW(&(ncm.lfMessageFont));
	if (hMessageFont == NULL)
		return ieLastErr("loading default messagebox font; this is the default UI font");

	if (initContainer(hDefaultIcon, hDefaultCursor) == 0)
		return ieLastErr("initializing uiWindowsMakeContainer() window class");

	hollowBrush = (HBRUSH) GetStockObject(HOLLOW_BRUSH);
	if (hollowBrush == NULL)
		return ieLastErr("getting hollow brush");

	ZeroMemory(&icc, sizeof (INITCOMMONCONTROLSEX));
	icc.dwSize = sizeof (INITCOMMONCONTROLSEX);
	icc.dwICC = wantedICCClasses;
	if (InitCommonControlsEx(&icc) == 0)
		return ieLastErr("initializing Common Controls");

	hr = CoInitialize(NULL);
	if (hr != S_OK && hr != S_FALSE)
		return ieHRESULT("initializing COM", hr);
	// LONGTERM initialize COM security
	// LONGTERM (windows vista) turn off COM exception handling

	hr = initDraw();
	if (hr != S_OK)
		return ieHRESULT("initializing Direct2D", hr);

	hr = uiprivInitDrawText();
	if (hr != S_OK)
		return ieHRESULT("initializing DirectWrite", hr);

	if (registerAreaClass(hDefaultIcon, hDefaultCursor) == 0)
		return ieLastErr("registering uiArea window class");

	if (registerMessageFilter() == 0)
		return ieLastErr("registering libui message filter");

	if (registerD2DScratchClass(hDefaultIcon, hDefaultCursor) == 0)
		return ieLastErr("initializing D2D scratch window class");

	hr = uiprivInitImage();
	if (hr != S_OK)
		return ieHRESULT("initializing WIC", hr);

	return NULL;
}

void uiUninit(void)
{
	uiprivUninitTimers();
	uiprivUninitImage();
	uninitMenus();
	unregisterD2DScratchClass();
	unregisterMessageFilter();
	unregisterArea();
	uiprivUninitDrawText();
	uninitDraw();
	CoUninitialize();
	if (DeleteObject(hollowBrush) == 0)
		logLastError(L"error freeing hollow brush");
	uninitContainer();
	if (DeleteObject(hMessageFont) == 0)
		logLastError(L"error deleting control font");
	unregisterWindowClass();
	// no need to delete the default icon or cursor; see http://stackoverflow.com/questions/30603077/
	uninitUtilWindow();
	uninitAlloc();
}

void uiFreeInitError(const char *err)
{
	if (*(err - 1) == '-')
		uiprivFree((void *) (err - 1));
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
		hInstance = hinstDLL;
	return TRUE;
}
