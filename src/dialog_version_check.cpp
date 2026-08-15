// Copyright (c) 2007, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#ifdef WITH_UPDATE_CHECKER

#include "compat.h"
#include "format.h"
#include "options.h"
#include "version.h"

#include <libaegisub/ass/string_codec.h>
#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/line_iterator.h>
#include <libaegisub/scoped_ptr.h>
#include <libaegisub/split.h>

#include <ctime>
#include <cctype>
#include <curl/curl.h>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <vector>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/hyperlink.h>
#include <wx/intl.h>
#include <wx/platinfo.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {
std::mutex VersionCheckLock;

struct AegisubUpdateDescription {
	std::string url;
	std::string friendly_name;
	std::string description;
};

struct SanaeRelease {
	int beta = 0;
	std::string tag;
	std::string page_url;
	std::string download_url;
	std::string description;
};

class SanaeUpdateDialog final : public wxDialog {
public:
	explicit SanaeUpdateDialog(SanaeRelease release)
	: wxDialog(nullptr, -1, _("Aegisub Sanae update available"))
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		auto title = new wxStaticText(this, -1, _("Aegisub Sanae update available"));
		auto title_font = title->GetFont();
		title_font.SetWeight(wxFONTWEIGHT_BOLD);
		title->SetFont(title_font);
		main->Add(title, 0, wxBOTTOM, 10);
		main->Add(new wxStaticText(this, -1, agi::wxformat(
			_("Installed: beta-%02d\nAvailable: beta-%02d"), GetSanaeBetaNumber(), release.beta)),
			0, wxBOTTOM, 10);

		if (!release.description.empty()) {
			auto notes = new wxTextCtrl(this, -1, to_wx(release.description), wxDefaultPosition,
				wxSize(520, 110), wxTE_MULTILINE | wxTE_READONLY);
			main->Add(notes, 0, wxEXPAND | wxBOTTOM, 10);
		}

		auto buttons = new wxBoxSizer(wxHORIZONTAL);
		auto download = new wxButton(this, wxID_HIGHEST + 410, _("Download"));
		auto page = new wxButton(this, wxID_HIGHEST + 411, _("Open release page"));
		auto later = new wxButton(this, wxID_CANCEL, _("Later"));
		buttons->Add(download, 0, wxRIGHT, 6);
		buttons->Add(page, 0, wxRIGHT, 6);
		buttons->Add(later);
		main->Add(buttons, 0, wxALIGN_RIGHT);

		download->Bind(wxEVT_BUTTON, [this, url = release.download_url.empty() ? release.page_url : release.download_url](wxCommandEvent&) {
			wxLaunchDefaultBrowser(to_wx(url), wxBROWSER_NEW_WINDOW);
			Close();
		});
		page->Bind(wxEVT_BUTTON, [url = release.page_url](wxCommandEvent&) {
			wxLaunchDefaultBrowser(to_wx(url), wxBROWSER_NEW_WINDOW);
		});
		later->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			OPT_SET("Version/Next Check")->SetInt(time(nullptr) + 7 * 24 * 60 * 60);
			Close();
		});
		Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Destroy(); });

		SetSizerAndFit(main);
		Centre();
		Show();
	}

	bool ShouldPreventAppExit() const override { return false; }
};

class VersionCheckerResultDialog final : public wxDialog {
	void OnCloseButton(wxCommandEvent &evt);
	void OnRemindMeLater(wxCommandEvent &evt);
	void OnClose(wxCloseEvent &evt);

	wxCheckBox *automatic_check_checkbox;

public:
	VersionCheckerResultDialog(wxString const& main_text, const std::vector<AegisubUpdateDescription> &updates);

	bool ShouldPreventAppExit() const override { return false; }
};

VersionCheckerResultDialog::VersionCheckerResultDialog(wxString const& main_text, const std::vector<AegisubUpdateDescription> &updates)
: wxDialog(nullptr, -1, _("Version Checker"))
{
	const int controls_width = 500;

	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText *text = new wxStaticText(this, -1, main_text);
	text->Wrap(controls_width);
	main_sizer->Add(text, 0, wxBOTTOM|wxEXPAND, 6);

	for (auto const& update : updates) {
		main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND|wxALL, 6);

		text = new wxStaticText(this, -1, to_wx(update.friendly_name));
		wxFont boldfont = text->GetFont();
		boldfont.SetWeight(wxFONTWEIGHT_BOLD);
		text->SetFont(boldfont);
		main_sizer->Add(text, 0, wxEXPAND|wxBOTTOM, 6);

		wxTextCtrl *descbox = new wxTextCtrl(this, -1, to_wx(update.description), wxDefaultPosition, wxSize(controls_width,60), wxTE_MULTILINE|wxTE_READONLY);
		main_sizer->Add(descbox, 0, wxEXPAND|wxBOTTOM, 6);

		main_sizer->Add(new wxHyperlinkCtrl(this, -1, to_wx(update.url), to_wx(update.url)), 0, wxALIGN_LEFT|wxBOTTOM, 6);
	}

	automatic_check_checkbox = new wxCheckBox(this, -1, _("&Auto Check for Updates"));
	automatic_check_checkbox->SetValue(OPT_GET("App/Auto/Check For Updates")->GetBool());

	wxButton *remind_later_button = nullptr;
	if (updates.size() > 0)
		remind_later_button = new wxButton(this, wxID_NO, _("Remind me again in a &week"));

	wxButton *close_button = new wxButton(this, wxID_OK, _("&Close"));
	SetAffirmativeId(wxID_OK);
	SetEscapeId(wxID_OK);

	if (updates.size())
		main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND|wxALL, 6);
	main_sizer->Add(automatic_check_checkbox, 0, wxEXPAND|wxBOTTOM, 6);

	auto button_sizer = new wxStdDialogButtonSizer();
	button_sizer->AddButton(close_button);
	if (remind_later_button)
		button_sizer->AddButton(remind_later_button);
	button_sizer->Realize();
	main_sizer->Add(button_sizer, 0, wxEXPAND, 0);

	wxSizer *outer_sizer = new wxBoxSizer(wxVERTICAL);
	outer_sizer->Add(main_sizer, 0, wxALL|wxEXPAND, 12);

	SetSizerAndFit(outer_sizer);
	Centre();
	Show();

	Bind(wxEVT_BUTTON, std::bind(&VersionCheckerResultDialog::Close, this, false), wxID_OK);
	Bind(wxEVT_BUTTON, &VersionCheckerResultDialog::OnRemindMeLater, this, wxID_NO);
	Bind(wxEVT_CLOSE_WINDOW, &VersionCheckerResultDialog::OnClose, this);
}

void VersionCheckerResultDialog::OnRemindMeLater(wxCommandEvent &) {
	// In one week
	time_t new_next_check_time = time(nullptr) + 7*24*60*60;
	OPT_SET("Version/Next Check")->SetInt(new_next_check_time);

	Close();
}

void VersionCheckerResultDialog::OnClose(wxCloseEvent &) {
	OPT_SET("App/Auto/Check For Updates")->SetBool(automatic_check_checkbox->GetValue());
	Destroy();
}

DEFINE_EXCEPTION(VersionCheckError, agi::Exception);

void PostErrorEvent(bool interactive, wxString const& error_text) {
	if (interactive) {
		agi::dispatch::Main().Async([=]{
			new VersionCheckerResultDialog(error_text, {});
		});
	}
}

static const char * GetOSShortName() {
	int osver_maj, osver_min;
	wxOperatingSystemId osid = wxGetOsVersion(&osver_maj, &osver_min);

	if (osid & wxOS_WINDOWS_NT) {
		if (osver_maj == 5 && osver_min == 0)
			return "win2k";
		else if (osver_maj == 5 && osver_min == 1)
			return "winxp";
		else if (osver_maj == 5 && osver_min == 2)
			return "win2k3"; // this is also xp64
		else if (osver_maj == 6 && osver_min == 0)
			return "win60"; // vista and server 2008
		else if (osver_maj == 6 && osver_min == 1)
			return "win61"; // 7 and server 2008r2
		else if (osver_maj == 6 && osver_min == 2)
			return "win62"; // 8 and server 2012
		else if (osver_maj == 6 && osver_min == 3)
			return "win63"; // 8.1 and server 2012r2
		else if (osver_maj == 10 && osver_min == 0)
			return "win10"; // 10 or 11 and server 2016/2019
		else
			return "windows"; // future proofing? I doubt we run on nt4
	}
	// CF returns 0x10 for some reason, which wx has recently started
	// turning into 10
	else if (osid & wxOS_MAC_OSX_DARWIN && (osver_maj == 0x10 || osver_maj == 10)) {
		// ugliest hack in the world? nah.
		static char osxstring[] = "osx00";
		char minor = osver_min >> 4;
		char patch = osver_min & 0x0F;
		osxstring[3] = minor + ((minor<=9) ? '0' : ('a'-1));
		osxstring[4] = patch + ((patch<=9) ? '0' : ('a'-1));
		return osxstring;
	}
	else if (osid & wxOS_UNIX_LINUX)
		return "linux";
	else if (osid & wxOS_UNIX_FREEBSD)
		return "freebsd";
	else if (osid & wxOS_UNIX_OPENBSD)
		return "openbsd";
	else if (osid & wxOS_UNIX_NETBSD)
		return "netbsd";
	else if (osid & wxOS_UNIX_SOLARIS)
		return "solaris";
	else if (osid & wxOS_UNIX_AIX)
		return "aix";
	else if (osid & wxOS_UNIX_HPUX)
		return "hpux";
	else if (osid & wxOS_UNIX)
		return "unix";
	else if (osid & wxOS_OS2)
		return "os2";
	else if (osid & wxOS_DOS)
		return "dos";
	else
		return "unknown";
}

#ifdef WIN32
typedef BOOL (WINAPI * PGetUserPreferredUILanguages)(DWORD dwFlags, PULONG pulNumLanguages, wchar_t *pwszLanguagesBuffer, PULONG pcchLanguagesBuffer);

// Try using Win 6+ functions if available
static wxString GetUILanguage() {
	agi::scoped_holder<HMODULE, BOOL (__stdcall *)(HMODULE)> kernel32(LoadLibraryW(L"kernel32.dll"), FreeLibrary);
	if (!kernel32) return "";

	PGetUserPreferredUILanguages gupuil = (PGetUserPreferredUILanguages)GetProcAddress(kernel32, "GetUserPreferredUILanguages");
	if (!gupuil) return "";

	ULONG numlang = 0, output_len = 0;
	if (gupuil(MUI_LANGUAGE_NAME, &numlang, 0, &output_len) != TRUE || !output_len)
		return "";

	std::vector<wchar_t> output(output_len);
	if (!gupuil(MUI_LANGUAGE_NAME, &numlang, &output[0], &output_len) || numlang < 1)
		return "";

	// We got at least one language, just treat it as the only, and a null-terminated string
	return &output[0];
}

static wxString GetSystemLanguage() {
	wxString res = GetUILanguage();
	if (!res)
		// On an old version of Windows, let's just return the LANGID as a string
		res = fmt_wx("x-win%04x", GetUserDefaultUILanguage());

	return res;
}
#elif __APPLE__
static wxString GetSystemLanguage() {
	CFLocaleRef locale = CFLocaleCopyCurrent();
	CFStringRef localeName = (CFStringRef)CFLocaleGetValue(locale, kCFLocaleIdentifier);

	char buf[128] = { 0 };
	CFStringGetCString(localeName, buf, sizeof buf, kCFStringEncodingUTF8);
	CFRelease(locale);

	return wxString::FromUTF8(buf);

}
#else
static wxString GetSystemLanguage() {
	return wxLocale::GetLanguageInfo(wxLocale::GetSystemLanguage())->CanonicalName;
}
#endif

static wxString GetAegisubLanguage() {
	return to_wx(OPT_GET("App/Language")->GetString());
}

size_t writeToStringCb(char *contents, size_t size, size_t nmemb, std::string *s) {
	s->append(contents, size * nmemb);
	return size * nmemb;
}

std::string ReadSanaeGithubToken() {
#ifdef _WIN32
	PCREDENTIALW raw = nullptr;
	if (!CredReadW(L"Aegisub Sanae/GitHub", CRED_TYPE_GENERIC, 0, &raw) || !raw)
		return {};

	struct CredentialDeleter {
		void operator()(CREDENTIALW *value) const { if (value) CredFree(value); }
	};
	std::unique_ptr<CREDENTIALW, CredentialDeleter> credential(raw);
	auto bytes = credential->CredentialBlob;
	auto size = credential->CredentialBlobSize;
	if (!bytes || size == 0) return {};

	bool looks_wide = size % sizeof(wchar_t) == 0;
	if (looks_wide) {
		for (DWORD index = 1; index < size; index += sizeof(wchar_t)) {
			if (bytes[index] != 0) {
				looks_wide = false;
				break;
			}
		}
	}
	if (looks_wide) {
		auto count = size / sizeof(wchar_t);
		wxString token(reinterpret_cast<wchar_t const *>(bytes), count);
		return from_wx(token.BeforeFirst(L'\0'));
	}
	return std::string(reinterpret_cast<char const *>(bytes), size);
#else
	return {};
#endif
}

std::string JsonString(json::Object const& object, char const *key) {
	auto value = object.find(key);
	if (value == object.end()) return {};
	try { return static_cast<std::string>(value->second); }
	catch (json::Exception const&) { return {}; }
}

int ParseSanaeBeta(std::string const& tag) {
	constexpr std::string_view prefix = "sanae-beta-";
	if (!tag.starts_with(prefix) || tag.size() == prefix.size()) return 0;
	int beta = 0;
	for (char value : std::string_view(tag).substr(prefix.size())) {
		if (!std::isdigit(static_cast<unsigned char>(value))) return 0;
		beta = beta * 10 + value - '0';
	}
	return beta;
}

SanaeRelease ParseSanaeRelease(std::string const& response) {
	std::stringstream stream(response);
	json::UnknownElement root;
	json::Reader::Read(root, stream);
	auto const& object = static_cast<json::Object const&>(root);

	SanaeRelease release;
	release.tag = JsonString(object, "tag_name");
	release.beta = ParseSanaeBeta(release.tag);
	release.page_url = JsonString(object, "html_url");
	release.description = JsonString(object, "body");

	auto assets = object.find("assets");
	if (assets != object.end()) {
		for (auto const& element : static_cast<json::Array const&>(assets->second)) {
			auto const& asset = static_cast<json::Object const&>(element);
			auto name = JsonString(asset, "name");
			auto url = JsonString(asset, "browser_download_url");
			if (url.empty()) continue;
			if (release.download_url.empty() || name.ends_with("Setup.exe"))
				release.download_url = std::move(url);
			if (name.ends_with("Setup.exe")) break;
		}
	}
	return release;
}

void DoCheck(bool interactive) {
	auto token = ReadSanaeGithubToken();
	if (token.empty())
		throw VersionCheckError(from_wx(_("Private Sanae update access is not configured. Add a Generic Credential named 'Aegisub Sanae/GitHub' to Windows Credential Manager; store a GitHub token with read access as its password.")));

	CURL *curl = curl_easy_init();
	if (!curl)
		throw VersionCheckError(from_wx(_("Curl could not be initialized.")));

	curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/yHdra/aegisubsanae/releases/latest");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	auto user_agent = agi::format("Aegisub-Sanae/%s", GetAegisubLongVersionString());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());

	curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
	headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
	auto authorization = "Authorization: Bearer " + token;
	headers = curl_slist_append(headers, authorization.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	std::string result;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStringCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);

	CURLcode res_code = curl_easy_perform(curl);
	long http_status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (res_code != CURLE_OK) {
		std::string err_msg = agi::format(_("Checking for updates failed: %s."), curl_easy_strerror(res_code));
		throw VersionCheckError(err_msg);
	}
	if (http_status == 401 || http_status == 403 || http_status == 404)
		throw VersionCheckError(from_wx(_("GitHub rejected access to the private Sanae repository. Check the token stored in Windows Credential Manager.")));
	if (http_status < 200 || http_status >= 300)
		throw VersionCheckError(agi::format("GitHub Releases returned HTTP %d", http_status));

	auto release = ParseSanaeRelease(result);
	if (release.beta <= 0 || release.page_url.empty())
		throw VersionCheckError(from_wx(_("The latest GitHub release does not use a valid sanae-beta-* tag.")));

	if (release.beta > GetSanaeBetaNumber()) {
		agi::dispatch::Main().Async([release = std::move(release)]() mutable {
			new SanaeUpdateDialog(std::move(release));
		});
	}
	else if (interactive) {
		agi::dispatch::Main().Async([] {
			new VersionCheckerResultDialog(_("Aegisub Sanae is up to date."), {});
		});
	}
}
}

void PerformVersionCheck(bool interactive) {
	agi::dispatch::Background().Async([=]{
		if (!interactive) {
			// Automatic checking enabled?
			if (!OPT_GET("App/Auto/Check For Updates")->GetBool())
				return;

			// Is it actually time for a check?
			time_t next_check = OPT_GET("Version/Next Check")->GetInt();
			if (next_check > time(nullptr))
				return;
		}

		if (!VersionCheckLock.try_lock()) return;

		try {
			DoCheck(interactive);
		}
			catch (const agi::Exception &e) {
				PostErrorEvent(interactive, fmt_tl(
					"There was an error checking for updates to Aegisub:\n%s\n\nIf other applications can access the Internet fine, this is probably a temporary server problem on our end.",
					e.GetMessage()));
			}
			catch (const std::exception &e) {
				PostErrorEvent(interactive, agi::wxformat(
					_("Checking for updates failed: %s."), to_wx(e.what())));
			}
		catch (...) {
			PostErrorEvent(interactive, _("An unknown error occurred while checking for updates to Aegisub."));
		}

		VersionCheckLock.unlock();

		agi::dispatch::Main().Async([]{
			time_t new_next_check_time = time(nullptr) + 60*60; // in one hour
			OPT_SET("Version/Next Check")->SetInt(new_next_check_time);
		});
	});
}

#endif
