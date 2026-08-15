// Copyright (c) 2026, Aegisub Sanae contributors

#include "dialog_sanae_connection.h"

#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "sanae_project.h"

#include <string>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

namespace {
class ConnectionDialog final : public wxDialog {
	SanaeProjectManager& manager;
	wxStaticText *state;
	wxStaticText *connected_name;
	wxStaticText *connected_device;
	wxTextCtrl *display_name;
	wxTextCtrl *device_name;
	wxTextCtrl *invitation;
	wxSizer *enrollment_fields;
	wxButton *connect;
	wxButton *check;
	wxButton *reenroll;

	void SetEnrollmentMode(bool enrolling) {
		GetSizer()->Show(enrollment_fields, enrolling, true);
		connect->Show(enrolling);
		check->Show(!enrolling);
		reenroll->Show(!enrolling);
		if (enrolling) {
			state->SetLabel(_("Not connected"));
			connected_name->SetLabel(to_wx("—"));
			connected_device->SetLabel(to_wx("—"));
			device_name->SetValue(wxGetHostName());
		}
		Layout();
		Fit();
	}

	void CheckConnection(bool show_success) {
		if (!manager.IsEnrolled()) {
			SetEnrollmentMode(true);
			return;
		}
		try {
			{
				wxBusyCursor busy;
				auto device = manager.CheckConnection();
				state->SetLabel(_("Connected"));
				connected_name->SetLabel(to_wx(device.display_name));
				connected_device->SetLabel(to_wx(device.device_name.empty() ? "—" : device.device_name));
			}
			SetEnrollmentMode(false);
			if (show_success)
				wxMessageBox(_("The server connection is working."), _("Server connection"),
					wxOK | wxICON_INFORMATION, this);
		}
		catch (std::exception const& error) {
			state->SetLabel(_("Connection check failed"));
			SetEnrollmentMode(false);
			wxMessageBox(agi::wxformat(
				_("The saved device could not be verified. The server may be temporarily unavailable.\n\nDetails: %s"),
				to_wx(error.what())), _("Server connection"), wxOK | wxICON_WARNING, this);
		}
	}

	void Enroll() {
		auto name = from_wx(display_name->GetValue());
		auto device = from_wx(device_name->GetValue());
		auto key = from_wx(invitation->GetValue());
		if (name.empty() || key.empty()) {
			wxMessageBox(_("Enter your name and invitation key."), _("Server connection"),
				wxOK | wxICON_ERROR, this);
			return;
		}
		try {
			{
				wxBusyCursor busy;
				manager.Enroll(name, device, key);
			}
			invitation->Clear();
			CheckConnection(false);
		}
		catch (std::exception const& error) {
			wxMessageBox(agi::wxformat(_("Could not connect this device to the server.\n\nDetails: %s"),
				to_wx(error.what())), _("Server connection"), wxOK | wxICON_ERROR, this);
		}
	}

	void Reenroll() {
		if (wxMessageBox(_("Remove the saved device token and register this device again?"),
			_("Register device again"), wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
		manager.ForgetEnrollment();
		connected_name->SetLabel(to_wx("—"));
		connected_device->SetLabel(to_wx("—"));
		SetEnrollmentMode(true);
	}

public:
	explicit ConnectionDialog(agi::Context *context)
	: wxDialog(context->parent, -1, _("Server connection"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, manager(*context->sanaeProject)
	{
		auto main = new wxBoxSizer(wxVERTICAL);
		auto details = new wxFlexGridSizer(2, 7, 10);
		details->AddGrowableCol(1, 1);
		details->Add(new wxStaticText(this, -1, _("Address:")), 0, wxALIGN_CENTER_VERTICAL);
		details->Add(new wxStaticText(this, -1, to_wx(manager.ServerBaseUrl())), 1, wxEXPAND);
		details->Add(new wxStaticText(this, -1, _("Status:")), 0, wxALIGN_CENTER_VERTICAL);
		details->Add(state = new wxStaticText(this, -1, _("Not connected")), 1, wxEXPAND);
		details->Add(new wxStaticText(this, -1, _("Name:")), 0, wxALIGN_CENTER_VERTICAL);
		details->Add(connected_name = new wxStaticText(this, -1, to_wx("—")), 1, wxEXPAND);
		details->Add(new wxStaticText(this, -1, _("Device:")), 0, wxALIGN_CENTER_VERTICAL);
		details->Add(connected_device = new wxStaticText(this, -1, to_wx("—")), 1, wxEXPAND);
		main->Add(details, 0, wxEXPAND | wxBOTTOM, 14);

		auto fields = new wxFlexGridSizer(2, 7, 10);
		fields->AddGrowableCol(1, 1);
		fields->Add(new wxStaticText(this, -1, _("Name:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(display_name = new wxTextCtrl(this, -1), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Device:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(device_name = new wxTextCtrl(this, -1, wxGetHostName()), 1, wxEXPAND);
		fields->Add(new wxStaticText(this, -1, _("Invitation key:")), 0, wxALIGN_CENTER_VERTICAL);
		fields->Add(invitation = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition,
			wxDefaultSize, wxTE_PASSWORD), 1, wxEXPAND);
		enrollment_fields = fields;
		main->Add(fields, 0, wxEXPAND | wxBOTTOM, 14);

		auto buttons = new wxBoxSizer(wxHORIZONTAL);
		connect = new wxButton(this, -1, _("Connect"));
		check = new wxButton(this, -1, _("Check connection"));
		reenroll = new wxButton(this, -1, _("Register device again"));
		buttons->Add(connect, 0, wxRIGHT, 6);
		buttons->Add(check, 0, wxRIGHT, 6);
		buttons->Add(reenroll, 0, wxRIGHT, 6);
		buttons->AddStretchSpacer();
		buttons->Add(new wxButton(this, wxID_CLOSE));
		main->Add(buttons, 0, wxEXPAND);
		SetSizer(main);
		SetMinSize(wxSize(570, 260));
		CentreOnParent();

		connect->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Enroll(); });
		check->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CheckConnection(true); });
		reenroll->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Reenroll(); });
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		if (manager.IsEnrolled()) CheckConnection(false);
		else SetEnrollmentMode(true);
	}
};
}

void ShowSanaeConnectionDialog(agi::Context *context) {
	ConnectionDialog(context).ShowModal();
}
