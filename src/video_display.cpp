// Copyright (c) 2005-2010, Rodrigo Braz Monteiro
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

/// @file video_display.cpp
/// @brief Control displaying a video frame obtained from the video context
/// @ingroup video main_ui
///

#include "video_display.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "async_video_provider.h"
#include "command/command.h"
#include "compat.h"
#include "format.h"
#include "gl_text.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "include/aegisub/menu.h"
#include "options.h"
#include "ocr/ocr_engine.h"
#include "project.h"
#include "retina_helper.h"
#include "selection_controller.h"
#include "spline_curve.h"
#include "utils.h"
#include "video_out_gl.h"
#include "video_controller.h"
#include "video_frame.h"
#include "visual_tool.h"
#include "value_event.h"

#include <libaegisub/color.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/fs.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <wx/combobox.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>

#ifdef HAVE_OPENGL_GL_H
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

/// Attribute list for gl canvases; set the canvases to doublebuffered rgba with an 8 bit stencil buffer
int attribList[] = { WX_GL_RGBA , WX_GL_DOUBLEBUFFER, WX_GL_STENCIL_SIZE, 8, 0 };

wxDEFINE_EVENT(EVT_IMAGE2TEXT_COMPLETE, ValueEvent<Image2TextThreadResult>);

enum {
	ID_IMAGE2TEXT_COPY = wxID_HIGHEST + 9050,
	ID_IMAGE2TEXT_COPY_LINES,
	ID_IMAGE2TEXT_MASK,
	ID_IMAGE2TEXT_CLEAR,
	ID_IMAGE2TEXT_EXIT
};

/// An OpenGL error occurred while uploading or displaying a frame
class OpenGlException final : public agi::Exception {
public:
	OpenGlException(const char *func, int err)
	: agi::Exception(agi::format("%s failed with error code %d", func, err))
	{ }
};

#define E(cmd) cmd; if (GLenum err = glGetError()) throw OpenGlException(#cmd, err)

VideoDisplay::VideoDisplay(wxToolBar *toolbar, bool freeSize, wxComboBox *zoomBox, wxWindow *parent, agi::Context *c)
: wxGLCanvas(parent, -1, attribList)
, autohideTools(OPT_GET("Tool/Visual/Autohide"))
, con(c)
, windowZoomValue(OPT_GET("Video/Default Zoom")->GetInt() * .125 + .125)
, videoZoomValue(1)
, toolBar(toolbar)
, zoomBox(zoomBox)
, freeSize(freeSize)
, retina_helper(agi::make_unique<RetinaHelper>(this))
, image2text_text(agi::make_unique<OpenGLText>())
, scale_factor(retina_helper->GetScaleFactor())
, scale_factor_connection(retina_helper->AddScaleFactorListener([=](int new_scale_factor) {
	double new_zoom = windowZoomValue * new_scale_factor / scale_factor;
	scale_factor = new_scale_factor;
	SetWindowZoom(new_zoom);
}))
{
	zoomBox->SetValue(fmt_wx("%g%%", windowZoomValue * 100.));
	zoomBox->Bind(wxEVT_COMBOBOX, &VideoDisplay::SetZoomFromBox, this);
	zoomBox->Bind(wxEVT_TEXT_ENTER, &VideoDisplay::SetZoomFromBoxText, this);

	con->videoController->Bind(EVT_FRAME_READY, &VideoDisplay::UploadFrameData, this);
	connections = agi::signal::make_vector({
		con->project->AddVideoProviderListener(&VideoDisplay::UpdateSize, this),
		con->videoController->AddARChangeListener(&VideoDisplay::UpdateSize, this),
	});

	Bind(wxEVT_PAINT, std::bind(&VideoDisplay::Render, this));
	Bind(wxEVT_SIZE, &VideoDisplay::OnSizeEvent, this);
	Bind(wxEVT_CONTEXT_MENU, &VideoDisplay::OnContextMenu, this);
	Bind(EVT_IMAGE2TEXT_COMPLETE, &VideoDisplay::OnImage2TextComplete, this);
	Bind(wxEVT_MENU, [=](wxCommandEvent&) { CopyImage2TextSelection(false); }, ID_IMAGE2TEXT_COPY);
	Bind(wxEVT_MENU, [=](wxCommandEvent&) { CopyImage2TextSelection(true); }, ID_IMAGE2TEXT_COPY_LINES);
	Bind(wxEVT_MENU, [=](wxCommandEvent&) { MaskImage2TextSelection(); }, ID_IMAGE2TEXT_MASK);
	Bind(wxEVT_MENU, [=](wxCommandEvent&) { ClearImage2TextSelection(); }, ID_IMAGE2TEXT_CLEAR);
	Bind(wxEVT_MENU, [=](wxCommandEvent&) { ExitImage2Text(); }, ID_IMAGE2TEXT_EXIT);
	Bind(wxEVT_ENTER_WINDOW, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_CHAR_HOOK, &VideoDisplay::OnKeyDown, this);
	Bind(wxEVT_LEAVE_WINDOW, &VideoDisplay::OnMouseLeave, this);
	Bind(wxEVT_LEFT_DCLICK, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_LEFT_DOWN, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_LEFT_UP, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MIDDLE_DOWN, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MIDDLE_UP, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MOTION, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MOUSEWHEEL, &VideoDisplay::OnMouseWheel, this);

	SetCursor(wxNullCursor);

	c->videoDisplay = this;

	con->videoController->JumpToFrame(con->videoController->GetFrameN());

	SetLayoutDirection(wxLayout_LeftToRight);
}

VideoDisplay::~VideoDisplay () {
	Unload();
	con->videoController->Unbind(EVT_FRAME_READY, &VideoDisplay::UploadFrameData, this);
}

bool VideoDisplay::InitContext() {
	if (!IsShownOnScreen())
		return false;

	// If this display is in a minimized detached dialog IsShownOnScreen will
	// return true, but the client size is guaranteed to be 0
	if (GetClientSize() == wxSize(0, 0))
		return false;

	if (!glContext)
		glContext = agi::make_unique<wxGLContext>(this);

	SetCurrent(*glContext);
	return true;
}

void VideoDisplay::UploadFrameData(FrameReadyEvent &evt) {
	pending_frame = evt.frame;
	CancelImage2TextIfFrameChanged(false);
	Render();
}

void VideoDisplay::Render() try {
	if (!con->project->VideoProvider() || !InitContext() || (!videoOut && !pending_frame))
		return;

	CancelImage2TextIfFrameChanged(false);

	if (!videoOut)
		videoOut = agi::make_unique<VideoOutGL>();

	if (!tool)
		cmd::call("video/tool/cross", con);

	try {
		if (pending_frame) {
			videoOut->UploadFrameData(*pending_frame);
			pending_frame.reset();
		}
	}
	catch (const VideoOutInitException& err) {
		wxLogError(
			"Failed to initialize video display. Closing other running "
			"programs and updating your video card drivers may fix this.\n"
			"Error message reported: %s",
			err.GetMessage());
		con->project->CloseVideo();
		return;
	}
	catch (const VideoOutRenderException& err) {
		wxLogError(
			"Could not upload video frame to graphics card.\n"
			"Error message reported: %s",
			err.GetMessage());
		return;
	}

	if (videoSize.GetWidth() == 0) videoSize.SetWidth(1);
	if (videoSize.GetHeight() == 0) videoSize.SetHeight(1);

	if (!viewport_height || !viewport_width)
		PositionVideo();

	videoOut->Render(viewport_left, viewport_bottom, viewport_width, viewport_height);

	int client_w, client_h;
	GetClientSize(&client_w, &client_h);
	E(glViewport(0, 0, client_w * scale_factor, client_h * scale_factor));

	E(glMatrixMode(GL_PROJECTION));
	E(glLoadIdentity());
	E(glOrtho(0.0f, std::max(client_w, 1), std::max(client_h, 1), 0.0f, -1000.0f, 1000.0f));

	if (OPT_GET("Video/Overscan Mask")->GetBool()) {
		double ar = con->videoController->GetAspectRatioValue();

		// Based on BBC's guidelines: http://www.bbc.co.uk/guidelines/dq/pdf/tv/tv_standards_london.pdf
		// 16:9 or wider
		if (ar > 1.75) {
			DrawOverscanMask(.1f, .05f);
			DrawOverscanMask(0.035f, 0.035f);
		}
		// Less wide than 16:9 (use 4:3 standard)
		else {
			DrawOverscanMask(.067f, .05f);
			DrawOverscanMask(0.033f, 0.035f);
		}
	}

	if (image2text_state != Image2TextState::Off)
		DrawImage2TextOverlay();
	else if ((mouse_pos || !autohideTools->GetBool()) && tool)
		tool->Draw();

	SwapBuffers();
}
catch (const agi::Exception &err) {
	wxLogError(
		"An error occurred trying to render the video frame on the screen.\n"
		"Error message reported: %s",
		err.GetMessage());
	con->project->CloseVideo();
}

void VideoDisplay::DrawOverscanMask(float horizontal_percent, float vertical_percent) const {
	Vector2D v = Vector2D(viewport_width, viewport_height) / scale_factor;
	Vector2D size = Vector2D(horizontal_percent, vertical_percent) * v;

	// Clockwise from top-left
	Vector2D corners[] = {
		size,
		Vector2D(viewport_width / scale_factor - size.X(), size),
		v - size,
		Vector2D(size, viewport_height  / scale_factor - size.Y())
	};

	// Shift to compensate for black bars
	Vector2D pos = Vector2D(viewport_left, viewport_top) / scale_factor;
	for (auto& corner : corners)
		corner = corner + pos;

	int count = 0;
	std::vector<float> points;
	for (size_t i = 0; i < 4; ++i) {
		size_t prev = (i + 3) % 4;
		size_t next = (i + 1) % 4;
		count += SplineCurve(
				(corners[prev] + corners[i] * 4) / 5,
				corners[i], corners[i],
				(corners[next] + corners[i] * 4) / 5)
			.GetPoints(points);
	}

	OpenGLWrapper gl;
	gl.SetFillColour(wxColor(30, 70, 200), .5f);
	gl.SetLineColour(*wxBLACK, 0, 1);

	std::vector<int> vstart(1, 0);
	std::vector<int> vcount(1, count);
	gl.DrawMultiPolygon(points, vstart, vcount, pos, v, true);
}

void VideoDisplay::DrawImage2TextMessage(std::string const& message, bool error) {
	if (!image2text_text)
		return;

	int width = 0;
	int height = 0;
	image2text_text->SetFont("Verdana", 12, true, false);
	image2text_text->SetColour(agi::Color(255, 255, 255, 255));
	image2text_text->GetExtent(message, width, height);

	Vector2D video_pos(viewport_left / scale_factor, viewport_top / scale_factor);
	Vector2D video_size(viewport_width / scale_factor, viewport_height / scale_factor);
	Vector2D top_left(
		video_pos.X() + std::max(16.0, (video_size.X() - width) / 2.0 - 14.0),
		video_pos.Y() + 18.0);
	Vector2D bottom_right(
		std::min(video_pos.X() + video_size.X() - 16.0, top_left.X() + width + 28.0),
		top_left.Y() + height + 18.0);

	OpenGLWrapper gl;
	gl.SetFillColour(*wxBLACK, error ? .68f : .55f);
	gl.SetLineColour(error ? wxColour(255, 120, 120) : wxColour(255, 255, 255), error ? .55f : .18f, 1);
	gl.DrawRectangle(top_left, bottom_right);
	image2text_text->Print(message, std::lround(top_left.X() + 14), std::lround(top_left.Y() + 9));
}

Vector2D VideoDisplay::Image2TextToDisplay(std::pair<int, int> const& point) const {
	auto provider = con->project->VideoProvider();
	if (!provider)
		return Vector2D();

	Vector2D video_pos(viewport_left / scale_factor, viewport_top / scale_factor);
	Vector2D video_size(viewport_width / scale_factor, viewport_height / scale_factor);
	return video_pos + Vector2D(
		point.first * video_size.X() / std::max(provider->GetWidth(), 1),
		point.second * video_size.Y() / std::max(provider->GetHeight(), 1));
}

Vector2D VideoDisplay::Image2TextToFrame(Vector2D const& point) const {
	auto provider = con->project->VideoProvider();
	if (!provider)
		return Vector2D();

	Vector2D video_pos(viewport_left / scale_factor, viewport_top / scale_factor);
	Vector2D video_size(viewport_width / scale_factor, viewport_height / scale_factor);
	if (video_size.X() <= 0 || video_size.Y() <= 0)
		return Vector2D();

	return Vector2D(
		(point.X() - video_pos.X()) * provider->GetWidth() / video_size.X(),
		(point.Y() - video_pos.Y()) * provider->GetHeight() / video_size.Y());
}

std::pair<Vector2D, Vector2D> VideoDisplay::Image2TextBounds(ocr::OCRLine const& line) const {
	if (line.box.empty())
		return {Vector2D(), Vector2D()};

	int min_x = line.box.front().first;
	int max_x = line.box.front().first;
	int min_y = line.box.front().second;
	int max_y = line.box.front().second;
	for (auto const& point : line.box) {
		min_x = std::min(min_x, point.first);
		max_x = std::max(max_x, point.first);
		min_y = std::min(min_y, point.second);
		max_y = std::max(max_y, point.second);
	}
	return {Vector2D(min_x, min_y), Vector2D(max_x, max_y)};
}

void VideoDisplay::DrawImage2TextRegion(size_t index) {
	if (index >= image2text_regions.size() || (index < image2text_masked.size() && image2text_masked[index]))
		return;

	auto const& line = image2text_regions[index];
	bool selected = image2text_selected.count(index) > 0;
	bool hovered = image2text_hovered == static_cast<int>(index);

	float fill_alpha = selected ? .58f : (hovered ? .42f : .26f);
	float outline_alpha = selected ? .78f : (hovered ? .42f : 0.f);

	OpenGLWrapper gl;
	gl.SetFillColour(*wxWHITE, fill_alpha);
	gl.SetLineColour(selected ? wxColour(82, 168, 255) : wxColour(255, 255, 255), outline_alpha, selected ? 2 : 1);

	std::vector<float> points;
	for (auto const& point : line.box) {
		auto display_point = Image2TextToDisplay(point);
		points.push_back(display_point.X());
		points.push_back(display_point.Y());
	}

	if (points.size() >= 6) {
		std::vector<int> start(1, 0);
		std::vector<int> count(1, static_cast<int>(points.size() / 2));
		Vector2D video_pos(viewport_left / scale_factor, viewport_top / scale_factor);
		Vector2D video_size(viewport_width / scale_factor, viewport_height / scale_factor);
		gl.DrawMultiPolygon(points, start, count, video_pos, video_size, false);
	}
	else {
		auto bounds = Image2TextBounds(line);
		if (bounds.first == bounds.second)
			return;
		gl.DrawRectangle(Image2TextToDisplay({static_cast<int>(bounds.first.X()), static_cast<int>(bounds.first.Y())}),
		                 Image2TextToDisplay({static_cast<int>(bounds.second.X()), static_cast<int>(bounds.second.Y())}));
	}
}

void VideoDisplay::DrawImage2TextOverlay() {
	E(glMatrixMode(GL_MODELVIEW));
	E(glLoadIdentity());

	if (image2text_state == Image2TextState::Loading) {
		DrawImage2TextMessage("Image2Text: scanning frame...", false);
		return;
	}

	if (image2text_state == Image2TextState::Error) {
		DrawImage2TextMessage(image2text_error.empty() ? "Image2Text: OCR failed" : image2text_error, true);
		return;
	}

	for (size_t i = 0; i < image2text_regions.size(); ++i)
		DrawImage2TextRegion(i);
}

void VideoDisplay::PositionVideo() {
	auto provider = con->project->VideoProvider();
	if (!provider || !IsShownOnScreen()) return;

	int client_w, client_h;
	GetClientSize(&client_w, &client_h);
	client_w *= scale_factor;
	client_h *= scale_factor;

	viewport_left = 0;
	viewport_bottom = client_h - videoSize.GetHeight();
	viewport_top = 0;
	viewport_width = videoSize.GetWidth();
	viewport_height = videoSize.GetHeight();

	if (freeSize) {
		int vidW = provider->GetWidth();
		int vidH = provider->GetHeight();

		AspectRatio arType = con->videoController->GetAspectRatioType();
		double displayAr = double(client_w) / client_h;
		double videoAr = arType == AspectRatio::Default ? double(vidW) / vidH : con->videoController->GetAspectRatioValue();

		// Window is wider than video, blackbox left/right
		if (displayAr - videoAr > 0.01) {
			int delta = client_w - videoAr * client_h;
			viewport_left = delta / 2;
		}
		// Video is wider than window, blackbox top/bottom
		else if (videoAr - displayAr > 0.01) {
			int delta = client_h - client_w / videoAr;
			viewport_top += delta / 2;
			viewport_bottom += delta / 2;
			viewport_height -= delta;
			viewport_width = viewport_height * videoAr;
		}
	}

	viewport_left += pan_x;
	viewport_top += pan_y;
	viewport_bottom -= pan_y;

	if (tool) {
		tool->SetClientSize(client_w, client_h);
		tool->SetDisplayArea(viewport_left / scale_factor, viewport_top / scale_factor,
		                     viewport_width / scale_factor, viewport_height / scale_factor);
	}
	Render();
}

void VideoDisplay::UpdateSize() {
	auto provider = con->project->VideoProvider();
	if (!provider || !IsShownOnScreen()) return;

	videoSize.Set(provider->GetWidth(), provider->GetHeight());
	videoSize *= windowZoomValue;
	if (con->videoController->GetAspectRatioType() != AspectRatio::Default)
		videoSize.SetWidth(videoSize.GetHeight() * con->videoController->GetAspectRatioValue());

	wxEventBlocker blocker(this);
	if (freeSize) {
		wxWindow *top = GetParent();
		while (!top->IsTopLevel()) top = top->GetParent();

		wxSize oldClientSize = GetClientSize();
		double csAr = (double)oldClientSize.GetWidth() / (double)oldClientSize.GetHeight();
		wxSize newClientSize = wxSize(std::lround(provider->GetHeight() * csAr), provider->GetHeight()) * windowZoomValue / scale_factor;
		wxSize oldSize = top->GetSize();
		top->SetSize(oldSize + (newClientSize - oldClientSize));
		SetClientSize(oldClientSize + (top->GetSize() - oldSize));
	}
	else {
		SetMinClientSize(videoSize / scale_factor);
		SetMaxClientSize(videoSize / scale_factor);

		GetGrandParent()->Layout();
	}
	videoSize *= videoZoomValue;

	PositionVideo();
}

void VideoDisplay::OnSizeEvent(wxSizeEvent &event) {
	if (freeSize) {
		/* If the video is not moved */
		if (videoZoomValue == 1.0f && pan_x == 0 && pan_y == 0)
			videoSize = GetClientSize() * scale_factor;
		/* If the video is moving, we only need to update the size in this case */
		else if (videoSize.GetWidth() == 0 && videoSize.GetHeight() == 0)
			videoSize = GetClientSize() * videoZoomValue * scale_factor;
		windowZoomValue = double(GetClientSize().GetHeight() * scale_factor) / con->project->VideoProvider()->GetHeight();
		zoomBox->ChangeValue(fmt_wx("%g%%", windowZoomValue * 100.));
		con->ass->Properties.video_zoom = windowZoomValue;
		UpdateSize();
	}
	else {
		PositionVideo();
	}
}

bool VideoDisplay::Image2TextPointInRegion(Vector2D const& point, ocr::OCRLine const& line) const {
	if (line.box.empty())
		return false;

	if (line.box.size() < 3) {
		auto bounds = Image2TextBounds(line);
		return point.X() >= bounds.first.X() && point.X() <= bounds.second.X() &&
		       point.Y() >= bounds.first.Y() && point.Y() <= bounds.second.Y();
	}

	bool inside = false;
	for (size_t i = 0, j = line.box.size() - 1; i < line.box.size(); j = i++) {
		auto const& a = line.box[i];
		auto const& b = line.box[j];
		if (((a.second > point.Y()) != (b.second > point.Y())) &&
			(point.X() < (b.first - a.first) * (point.Y() - a.second) / static_cast<double>(b.second - a.second) + a.first))
			inside = !inside;
	}
	return inside;
}

int VideoDisplay::HitTestImage2Text(Vector2D const& point) const {
	auto frame_point = Image2TextToFrame(point);
	for (size_t i = image2text_regions.size(); i > 0; --i) {
		size_t index = i - 1;
		if (index < image2text_masked.size() && image2text_masked[index])
			continue;
		if (Image2TextPointInRegion(frame_point, image2text_regions[index]))
			return static_cast<int>(index);
	}
	return -1;
}

bool VideoDisplay::Image2TextDragIntersects(size_t index) const {
	if (index >= image2text_regions.size())
		return false;

	Vector2D drag_min(
		std::min(image2text_drag_start.X(), image2text_drag_current.X()),
		std::min(image2text_drag_start.Y(), image2text_drag_current.Y()));
	Vector2D drag_max(
		std::max(image2text_drag_start.X(), image2text_drag_current.X()),
		std::max(image2text_drag_start.Y(), image2text_drag_current.Y()));

	auto bounds = Image2TextBounds(image2text_regions[index]);
	if (bounds.first == bounds.second)
		return false;

	Vector2D display_min = Image2TextToDisplay({static_cast<int>(bounds.first.X()), static_cast<int>(bounds.first.Y())});
	Vector2D display_max = Image2TextToDisplay({static_cast<int>(bounds.second.X()), static_cast<int>(bounds.second.Y())});
	return display_min.X() <= drag_max.X() && display_max.X() >= drag_min.X() &&
	       display_min.Y() <= drag_max.Y() && display_max.Y() >= drag_min.Y();
}

void VideoDisplay::SelectImage2TextRegionAt(Vector2D const& point, bool clear_first) {
	if (clear_first)
		image2text_selected.clear();

	int hit = HitTestImage2Text(point);
	if (hit >= 0)
		image2text_selected.insert(static_cast<size_t>(hit));
}

void VideoDisplay::SelectImage2TextDragIntersections() {
	for (size_t i = 0; i < image2text_regions.size(); ++i) {
		if (i < image2text_masked.size() && image2text_masked[i])
			continue;
		if (Image2TextDragIntersects(i))
			image2text_selected.insert(i);
	}
}

void VideoDisplay::SelectAllImage2TextRegions() {
	image2text_selected.clear();
	for (size_t i = 0; i < image2text_regions.size(); ++i) {
		if (i >= image2text_masked.size() || !image2text_masked[i])
			image2text_selected.insert(i);
	}
	Render();
}

std::vector<ocr::OCRLine> VideoDisplay::SelectedImage2TextLines() const {
	std::vector<ocr::OCRLine> lines;
	for (auto index : image2text_selected) {
		if (index < image2text_regions.size() && (index >= image2text_masked.size() || !image2text_masked[index]))
			lines.push_back(image2text_regions[index]);
	}

	auto bounds_for = [this](ocr::OCRLine const& line) {
		return Image2TextBounds(line);
	};
	auto vertical = [this](ocr::OCRLine const& line) {
		auto bounds = Image2TextBounds(line);
		return (bounds.second.Y() - bounds.first.Y()) > (bounds.second.X() - bounds.first.X()) * 1.25;
	};

	std::sort(lines.begin(), lines.end(), [&](ocr::OCRLine const& a, ocr::OCRLine const& b) {
		auto a_bounds = bounds_for(a);
		auto b_bounds = bounds_for(b);
		bool a_vertical = vertical(a);
		bool b_vertical = vertical(b);
		if (a_vertical && b_vertical) {
			if (std::abs(a_bounds.second.X() - b_bounds.second.X()) > 12)
				return a_bounds.second.X() > b_bounds.second.X();
			return a_bounds.first.Y() < b_bounds.first.Y();
		}

		if (std::abs(a_bounds.first.Y() - b_bounds.first.Y()) > 18)
			return a_bounds.first.Y() < b_bounds.first.Y();
		return a_bounds.first.X() < b_bounds.first.X();
	});

	return lines;
}

std::string VideoDisplay::Image2TextSelectionText(bool line_breaks) const {
	return ocr::NormalizeText(SelectedImage2TextLines(), line_breaks);
}

void VideoDisplay::CopyImage2TextSelection(bool line_breaks) {
	CancelImage2TextIfFrameChanged(true);
	if (image2text_state == Image2TextState::Off)
		return;

	auto text = Image2TextSelectionText(line_breaks);
	if (!text.empty())
		SetClipboard(text);
}

AssDialogue *VideoDisplay::LiveImage2TextLine(AssDialogue *line) const {
	if (!line)
		return nullptr;

	for (auto& event : con->ass->Events) {
		if (&event == line)
			return line;
	}
	return nullptr;
}

AssDialogue *VideoDisplay::Image2TextInsertionLine() const {
	if (auto line = LiveImage2TextLine(image2text_anchor_line))
		return line;

	if (auto line = LiveImage2TextLine(con->selectionController->GetActiveLine()))
		return line;

	for (auto line : con->selectionController->GetSortedSelection()) {
		if (auto live_line = LiveImage2TextLine(line))
			return live_line;
	}

	return nullptr;
}

std::string VideoDisplay::Image2TextMaskDrawing() const {
	int video_w = image2text_video_w;
	int video_h = image2text_video_h;
	if (auto provider = con->project->VideoProvider()) {
		if (video_w <= 0)
			video_w = provider->GetWidth();
		if (video_h <= 0)
			video_h = provider->GetHeight();
	}

	if (video_w <= 0 || video_h <= 0 || image2text_script_w <= 0 || image2text_script_h <= 0)
		return std::string();

	auto to_script = [&](double frame_x, double frame_y) {
		frame_x = std::min(std::max(frame_x, 0.0), static_cast<double>(video_w));
		frame_y = std::min(std::max(frame_y, 0.0), static_cast<double>(video_h));
		return std::make_pair(
			std::lround(frame_x * image2text_script_w / video_w),
			std::lround(frame_y * image2text_script_h / video_h));
	};

	std::ostringstream drawing;
	drawing << "{\\an7\\p1\\pos(0,0)\\bord0\\shad0\\c&H000000&}";
	bool drew_shape = false;

	for (auto const& line : SelectedImage2TextLines()) {
		std::vector<std::pair<double, double>> points;
		auto bounds = Image2TextBounds(line);
		if (bounds.first == bounds.second)
			continue;

		const double padding = 4.0;
		if (line.box.size() >= 3) {
			double center_x = (bounds.first.X() + bounds.second.X()) / 2.0;
			double center_y = (bounds.first.Y() + bounds.second.Y()) / 2.0;
			for (auto const& point : line.box) {
				double x = point.first;
				double y = point.second;
				if (x < center_x)
					x -= padding;
				else if (x > center_x)
					x += padding;
				if (y < center_y)
					y -= padding;
				else if (y > center_y)
					y += padding;
				points.emplace_back(x, y);
			}
		}
		else {
			double left = bounds.first.X() - padding;
			double top = bounds.first.Y() - padding;
			double right = bounds.second.X() + padding;
			double bottom = bounds.second.Y() + padding;
			points = {
				{left, top},
				{right, top},
				{right, bottom},
				{left, bottom}
			};
		}

		if (points.size() < 3)
			continue;

		if (drew_shape)
			drawing << ' ';

		auto first = to_script(points.front().first, points.front().second);
		drawing << "m " << first.first << ' ' << first.second;
		for (size_t i = 1; i < points.size(); ++i) {
			auto point = to_script(points[i].first, points[i].second);
			drawing << " l " << point.first << ' ' << point.second;
		}
		drew_shape = true;
	}

	return drew_shape ? drawing.str() : std::string();
}

void VideoDisplay::MaskImage2TextSelection() {
	CancelImage2TextIfFrameChanged(true);
	if (image2text_state == Image2TextState::Off)
		return;

	if (image2text_selected.empty())
		return;

	auto drawing = Image2TextMaskDrawing();
	if (drawing.empty())
		return;

	auto anchor_line = LiveImage2TextLine(image2text_anchor_line);
	auto insert_above = anchor_line ? anchor_line : Image2TextInsertionLine();
	auto style_source = anchor_line ? anchor_line : insert_above;
	auto mask_line = new AssDialogue;

	mask_line->Comment = false;
	if (style_source) {
		mask_line->Style = style_source->Style;
		mask_line->Margin = style_source->Margin;
		mask_line->Layer = style_source->Layer + 1;
	}

	if (anchor_line && anchor_line->Start <= image2text_ocr_time && image2text_ocr_time < anchor_line->End) {
		mask_line->Start = anchor_line->Start;
		mask_line->End = anchor_line->End;
	}
	else {
		int duration = std::max(1, image2text_one_frame_ms);
		mask_line->Start = image2text_ocr_time;
		mask_line->End = image2text_ocr_time + duration;
	}

	if (mask_line->End <= mask_line->Start)
		mask_line->End = mask_line->Start + std::max(1, image2text_one_frame_ms);

	mask_line->Text = drawing;

	if (insert_above)
		con->ass->Events.insert(con->ass->iterator_to(*insert_above), *mask_line);
	else
		con->ass->Events.push_back(*mask_line);

	con->ass->Commit(_("Image2Text mask"), AssFile::COMMIT_DIAG_ADDREM, -1, mask_line);
	con->selectionController->SetSelectionAndActive({mask_line}, mask_line);

	for (auto index : image2text_selected) {
		if (index < image2text_masked.size())
			image2text_masked[index] = true;
	}
	ClearImage2TextSelection();
}

void VideoDisplay::ClearImage2TextSelection() {
	image2text_selected.clear();
	Render();
}

void VideoDisplay::ExitImage2Text() {
	ClearImage2TextState(true);
}

void VideoDisplay::ClearImage2TextState(bool render) {
	if (HasCapture())
		ReleaseMouse();
	image2text_state = Image2TextState::Off;
	image2text_regions.clear();
	image2text_masked.clear();
	image2text_selected.clear();
	image2text_hovered = -1;
	image2text_dragging = false;
	image2text_error.clear();
	image2text_anchor_line = nullptr;
	image2text_ocr_frame = -1;
	image2text_ocr_time = 0;
	image2text_one_frame_ms = 100;
	image2text_script_w = 0;
	image2text_script_h = 0;
	image2text_video_w = 0;
	image2text_video_h = 0;
	if (render)
		Render();
}

void VideoDisplay::CancelImage2TextIfFrameChanged(bool render) {
	if (image2text_state != Image2TextState::Loading && image2text_state != Image2TextState::Ready)
		return;

	if (image2text_ocr_frame >= 0 && con->videoController->GetFrameN() != image2text_ocr_frame)
		ClearImage2TextState(render);
}

bool VideoDisplay::HandleImage2TextKey(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE) {
		ExitImage2Text();
		return true;
	}

	if (event.ControlDown() && (event.GetKeyCode() == 'C' || event.GetKeyCode() == 'c')) {
		CopyImage2TextSelection(false);
		return true;
	}

	return false;
}

bool VideoDisplay::HandleImage2TextMouse(wxMouseEvent& event) {
	if (image2text_state == Image2TextState::Loading)
		return true;

	Vector2D point(event.GetPosition());
	int previous_hovered = image2text_hovered;
	image2text_hovered = HitTestImage2Text(point);

	if (event.LeftDClick()) {
		SelectAllImage2TextRegions();
		return true;
	}

	if (event.LeftDown()) {
		image2text_dragging = true;
		image2text_drag_start = point;
		image2text_drag_current = point;
		SelectImage2TextRegionAt(point, true);
		if (!HasCapture())
			CaptureMouse();
		Render();
		return true;
	}

	if (event.Dragging() && image2text_dragging && event.LeftIsDown()) {
		image2text_drag_current = point;
		SelectImage2TextRegionAt(point, false);
		SelectImage2TextDragIntersections();
		Render();
		return true;
	}

	if (event.LeftUp() && image2text_dragging) {
		image2text_dragging = false;
		if (HasCapture())
			ReleaseMouse();
		Render();
		return true;
	}

	if (previous_hovered != image2text_hovered)
		Render();

	return true;
}

void VideoDisplay::OpenImage2TextMenu(wxPoint const& point) {
	wxMenu menu;
	auto copy = menu.Append(ID_IMAGE2TEXT_COPY, _("Copy text"));
	auto copy_lines = menu.Append(ID_IMAGE2TEXT_COPY_LINES, _("Copy text with line breaks"));
	auto mask = menu.Append(ID_IMAGE2TEXT_MASK, _("Mask this area"));
	if (image2text_selected.empty()) {
		copy->Enable(false);
		copy_lines->Enable(false);
		mask->Enable(false);
	}
	menu.Append(ID_IMAGE2TEXT_CLEAR, _("Clear selection"));
	menu.AppendSeparator();
	menu.Append(ID_IMAGE2TEXT_EXIT, _("Exit Image2Text"));
	PopupMenu(&menu, point);
}

void VideoDisplay::OnMouseEvent(wxMouseEvent& event) {
	if (event.ButtonDown())
		SetFocus();

	last_mouse_pos = mouse_pos = event.GetPosition();

	if (image2text_state != Image2TextState::Off) {
		CancelImage2TextIfFrameChanged(true);
		if (image2text_state == Image2TextState::Off)
			return;
		HandleImage2TextMouse(event);
		return;
	}

	if (event.MiddleDown()) {
		panning = true;
		pan_last_pos = event.GetPosition();
	}
	else if (event.MiddleUp()) {
		panning = false;
	}

	// Never pan unless middle button is currently held.
	if (panning && !event.MiddleIsDown())
		panning = false;

	if (panning && event.Dragging() && event.MiddleIsDown()) {
		pan_x += event.GetX() - pan_last_pos.X();
		pan_y += event.GetY() - pan_last_pos.Y();
		pan_last_pos = event.GetPosition();

		PositionVideo();
	}

	///

	if (tool)
		tool->OnMouseEvent(event);
}

void VideoDisplay::OnMouseLeave(wxMouseEvent& event) {
	mouse_pos = Vector2D();
	panning = false;
	if (image2text_state != Image2TextState::Off) {
		if (image2text_hovered != -1) {
			image2text_hovered = -1;
			Render();
		}
		return;
	}
	if (tool)
		tool->OnMouseEvent(event);
}

void VideoDisplay::OnMouseWheel(wxMouseEvent& event) {
	if (image2text_state != Image2TextState::Off)
		return;

	if (int wheel = event.GetWheelRotation()) {
		if (ForwardMouseWheelEvent(this, event) && !OPT_GET("Video/Disable Scroll Zoom")->GetBool()) {
			if (OPT_GET("Video/Reverse Zoom")->GetBool()) {
				wheel = -wheel;
			}
			if (event.ControlDown() == OPT_GET("Video/Default to Video Zoom")->GetBool()) {
				SetWindowZoom(windowZoomValue + .125 * (wheel / event.GetWheelDelta()));
			} else {
				SetVideoZoom(wheel / event.GetWheelDelta());
			}
		}
	}
}

void VideoDisplay::OnContextMenu(wxContextMenuEvent& event) {
	if (image2text_state != Image2TextState::Off) {
		CancelImage2TextIfFrameChanged(true);
		if (image2text_state == Image2TextState::Off) {
			if (!context_menu) context_menu = menu::GetMenu("video_context", (wxID_HIGHEST + 1) + 9000, con);
			SetCursor(wxNullCursor);
			menu::OpenPopupMenu(context_menu.get(), this);
			return;
		}

		wxPoint point = event.GetPosition();
		if (point == wxDefaultPosition)
			point = wxPoint(std::lround(last_mouse_pos.X()), std::lround(last_mouse_pos.Y()));
		else
			point = ScreenToClient(point);

		int hit = HitTestImage2Text(Vector2D(point));
		if (hit >= 0 && !image2text_selected.count(static_cast<size_t>(hit))) {
			image2text_selected.clear();
			image2text_selected.insert(static_cast<size_t>(hit));
			Render();
		}

		OpenImage2TextMenu(point);
		return;
	}

	if (!context_menu) context_menu = menu::GetMenu("video_context", (wxID_HIGHEST + 1) + 9000, con);
	SetCursor(wxNullCursor);
	menu::OpenPopupMenu(context_menu.get(), this);
}

void VideoDisplay::OnKeyDown(wxKeyEvent &event) {
	if (image2text_state != Image2TextState::Off) {
		CancelImage2TextIfFrameChanged(true);
		if (image2text_state != Image2TextState::Off && HandleImage2TextKey(event))
			return;
	}

	hotkey::check("Video", con, event);
}

void VideoDisplay::StartImage2TextOCR() {
	if (image2text_state == Image2TextState::Loading)
		return;

	image2text_regions.clear();
	image2text_masked.clear();
	image2text_selected.clear();
	image2text_hovered = -1;
	image2text_dragging = false;
	image2text_error.clear();

	auto provider = con->project->VideoProvider();
	if (!provider) {
		image2text_state = Image2TextState::Error;
		image2text_error = "Image2Text: no video is loaded";
		Render();
		return;
	}

	ocr::OCREngine engine;
	ocr::OCROptions options;
	options.keep_line_breaks = OPT_GET("Tool/OCR/Keep Line Breaks")->GetBool();
	options.language = OPT_GET("Tool/OCR/Language")->GetString();
	if (options.language.empty())
		options.language = "japanese";

	auto diagnostic = engine.GetDiagnostic(options);
	if (!diagnostic.empty()) {
		image2text_state = Image2TextState::Error;
		image2text_error = "Image2Text: OCR runtime unavailable";
		Render();
		return;
	}

	int frame_number = con->videoController->GetFrameN();
	image2text_anchor_line = con->selectionController->GetActiveLine();
	if (!image2text_anchor_line) {
		auto selection = con->selectionController->GetSortedSelection();
		if (!selection.empty())
			image2text_anchor_line = selection.front();
	}
	image2text_ocr_frame = frame_number;
	image2text_ocr_time = con->videoController->TimeAtFrame(frame_number, agi::vfr::START);
	image2text_one_frame_ms = con->videoController->TimeAtFrame(frame_number + 1, agi::vfr::START) - image2text_ocr_time;
	if (image2text_one_frame_ms <= 0)
		image2text_one_frame_ms = con->videoController->TimeAtFrame(frame_number, agi::vfr::END) - image2text_ocr_time;
	if (image2text_one_frame_ms <= 0)
		image2text_one_frame_ms = 100;
	con->ass->GetResolution(image2text_script_w, image2text_script_h);
	image2text_video_w = provider->GetWidth();
	image2text_video_h = provider->GetHeight();

	wxString temp_file = wxFileName::CreateTempFileName("aegisub-image2text-");
	if (temp_file.empty()) {
		image2text_state = Image2TextState::Error;
		image2text_error = "Image2Text: failed to create temporary frame image";
		Render();
		return;
	}

	agi::fs::path image_path(std::wstring(temp_file.wc_str()));
	try {
		auto image = GetImage(*provider->GetFrame(frame_number, con->project->Timecodes().TimeAtFrame(frame_number), true));
		if (!image.SaveFile(temp_file, wxBITMAP_TYPE_PNG))
			throw agi::EnvironmentError("failed to save frame image");
	}
	catch (agi::Exception const&) {
		try {
			agi::fs::Remove(image_path);
		}
		catch (agi::Exception const&) {
		}
		image2text_state = Image2TextState::Error;
		image2text_error = "Image2Text: failed to save current frame";
		Render();
		return;
	}
	catch (std::exception const&) {
		try {
			agi::fs::Remove(image_path);
		}
		catch (agi::Exception const&) {
		}
		image2text_state = Image2TextState::Error;
		image2text_error = "Image2Text: failed to save current frame";
		Render();
		return;
	}

	image2text_state = Image2TextState::Loading;
	SetFocus();
	Render();

	auto handler = this;
	agi::dispatch::Background().Async([handler, engine, options, image_path, frame_number] {
		Image2TextThreadResult thread_result;
		thread_result.frame_number = frame_number;
		thread_result.result = engine.RecognizeImage(image_path, options);
		try {
			agi::fs::Remove(image_path);
		}
		catch (agi::Exception const&) {
		}
		handler->AddPendingEvent(ValueEvent<Image2TextThreadResult>(EVT_IMAGE2TEXT_COMPLETE, -1, std::move(thread_result)));
	});
}

void VideoDisplay::OnImage2TextComplete(ValueEvent<Image2TextThreadResult>& event) {
	if (image2text_state == Image2TextState::Off)
		return;

	auto const& thread_result = event.Get();
	if (thread_result.frame_number != image2text_ocr_frame)
		return;

	if (con->videoController->GetFrameN() != thread_result.frame_number) {
		ClearImage2TextState(true);
		return;
	}

	auto const& result = thread_result.result;
	if (!result.ok) {
		image2text_state = Image2TextState::Error;
		image2text_regions.clear();
		image2text_masked.clear();
		image2text_selected.clear();
		image2text_hovered = -1;
		auto first_line_end = result.diagnostic.find('\n');
		auto message = first_line_end == std::string::npos ? result.diagnostic : result.diagnostic.substr(0, first_line_end);
		image2text_error = message.empty() ? "Image2Text: OCR failed" : "Image2Text: " + message;
		Render();
		return;
	}

	image2text_state = Image2TextState::Ready;
	image2text_regions = result.lines;
	image2text_masked.assign(image2text_regions.size(), false);
	image2text_selected.clear();
	image2text_hovered = -1;
	image2text_dragging = false;
	image2text_error.clear();
	Render();
}

void VideoDisplay::ResetPan() {
	pan_x = pan_y = 0;
	videoZoomValue = 1;
	UpdateSize();
	PositionVideo();
}

void VideoDisplay::SetWindowZoom(double value, bool adjust_pan) {
	if (value == 0) return;
	value = std::max(value, .125);
	if (adjust_pan) {
		pan_x *= value / windowZoomValue;
		pan_y *= value / windowZoomValue;
	}
	windowZoomValue = value;
	size_t selIndex = windowZoomValue / .125 - 1;
	if (selIndex < zoomBox->GetCount())
		zoomBox->SetSelection(selIndex);
	zoomBox->ChangeValue(fmt_wx("%g%%", windowZoomValue * 100.));
	con->ass->Properties.video_zoom = windowZoomValue;
	UpdateSize();
}

void VideoDisplay::SetVideoZoom(int step) {
	if (step == 0) return;
	double newVideoZoom = videoZoomValue + (.125 * step) * videoZoomValue;
	if (newVideoZoom < 0.125 || newVideoZoom > 10.0)
		return;

	// With the current blackbox algorithm in PositionVideo(), viewport_{width,height} could go negative. Stop that here
	wxSize cs = GetClientSize();
	wxSize videoNewSize = videoSize * (newVideoZoom / videoZoomValue);
	float windowAR = (float)cs.GetWidth() / cs.GetHeight();
	float videoAR = (float)videoNewSize.GetWidth() / videoNewSize.GetHeight();
	if (windowAR < videoAR) {
		int delta = cs.GetHeight() - cs.GetWidth() / videoAR;
		if (videoNewSize.GetHeight() - delta < 0)
			return;
	}

	// Mouse coordinates, relative to the video, at the current zoom level
	Vector2D mp = last_mouse_pos - Vector2D(viewport_left, viewport_top) / scale_factor;

	// The video size will change by this many pixels
	int pixelChangeW = std::lround(videoSize.GetWidth() * (newVideoZoom / videoZoomValue - 1.0));
	int pixelChangeH = std::lround(videoSize.GetHeight() * (newVideoZoom / videoZoomValue - 1.0));

	AsyncVideoProvider *provider = con->project->VideoProvider();
	double arfactor = (double) provider->GetHeight() * (double) videoSize.GetWidth() / (double) provider->GetWidth() / (double) videoSize.GetHeight();

	pan_x -= pixelChangeW * (mp.X() / videoSize.GetWidth() * arfactor);
	pan_y -= pixelChangeH * (mp.Y() / videoSize.GetHeight());

	videoZoomValue = newVideoZoom;
	UpdateSize();
}

void VideoDisplay::SetZoomFromBox(wxCommandEvent &) {
	int sel = zoomBox->GetSelection();
	if (sel != wxNOT_FOUND) {
		windowZoomValue = (sel + 1) * .125;
		con->ass->Properties.video_zoom = windowZoomValue;
		UpdateSize();
	}
}

void VideoDisplay::SetZoomFromBoxText(wxCommandEvent &) {
	wxString strValue = zoomBox->GetValue();
	if (strValue.EndsWith("%"))
		strValue.RemoveLast();

	double value;
	if (strValue.ToDouble(&value))
		SetWindowZoom(value / 100.);
}

void VideoDisplay::SetTool(std::unique_ptr<VisualToolBase> new_tool) {
	// Set the tool first to prevent repeated initialization from VideoDisplay::Render
	tool = std::move(new_tool);

	// Hide the tool bar first to eliminate unecessary size changes
	toolBar->Show(false);
	toolBar->ClearTools();
	tool->SetToolbar(toolBar);

	// Update size as the new typesetting tool may have changed the subtoolbar size
	if (!freeSize)
		UpdateSize();
	else {
		// UpdateSize fits the window to the video, which we don't want to do
		GetGrandParent()->Layout();
		tool->SetDisplayArea(viewport_left / scale_factor, viewport_top / scale_factor,
		                     viewport_width / scale_factor, viewport_height / scale_factor);
	}
}

bool VideoDisplay::ToolIsType(std::type_info const& type) const {
	return tool && typeid(*tool) == type;
}

Vector2D VideoDisplay::GetMousePosition() const {
	return last_mouse_pos ? tool->ToScriptCoords(last_mouse_pos) : last_mouse_pos;
}

void VideoDisplay::Unload() {
	if (glContext) {
		SetCurrent(*glContext);
	}
	videoOut.reset();
	tool.reset();
	glContext.reset();
	pending_frame.reset();
}
