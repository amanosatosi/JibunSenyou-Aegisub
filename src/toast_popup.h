#pragma once

#include <wx/string.h>

class wxWindow;

/// Display a short-lived toast popup centered near the top of \p parent.
/// \param parent Window used both for parenting and placement.
/// \param message Message to display inside the toast.
void ShowToast(wxWindow *parent, const wxString &message);
