/* Reverse Engineer's Hex Editor
 * Copyright (C) 2022 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "DetachableNotebook.hpp"
#include "mainwindow.hpp"

wxDEFINE_EVENT(REHex::EVT_PAGE_DETACHED, REHex::DetachedPageEvent);
wxDEFINE_EVENT(REHex::EVT_PAGE_DROPPED, REHex::DetachedPageEvent);

REHex::DetachableNotebook::DetachableNotebook(wxWindow *parent, wxWindowID id, const void *page_drop_group, wxEvtHandler *detached_page_handler, const wxPoint &pos, const wxSize &size, long style):
	wxAuiNotebook(parent, id, pos, size, style),
	page_drop_group(page_drop_group),
	detached_page_handler(detached_page_handler != NULL ? detached_page_handler : this)
{
	Bind(wxEVT_AUINOTEBOOK_DRAG_MOTION, &REHex::DetachableNotebook::OnTabDragMotion, this);
}

REHex::DetachableNotebook::~DetachableNotebook() {}

void REHex::DetachableNotebook::OnTabDragMotion(wxAuiNotebookEvent &event)
{
	wxAuiTabCtrl* tab_ctrl = dynamic_cast<wxAuiTabCtrl*>(event.GetEventObject());
	assert(tab_ctrl != NULL);
	
	wxPoint screen_pt = wxGetMousePosition();
	
	if(!tab_ctrl->GetScreenRect().Contains(screen_pt))
	{
		if(DragFrame::get_instance() != NULL)
		{
			/* Sometimes (only seen on macOS) we get multiple wxEVT_AUINOTEBOOK_DRAG_MOTION
			 * events, which can cause us to try setting up multiple drag operations at the
			 * same time without this check.
			*/
			return;
		}
		
		assert(tab_ctrl->HasCapture());
		
		if(tab_ctrl->HasCapture())
		{
			tab_ctrl->ReleaseMouse();
			
			wxMouseCaptureLostEvent e;
			tab_ctrl->GetEventHandler()->ProcessEvent(e);
		}
		
		wxWindow *page = GetPage(event.GetSelection());
		RemovePage(event.GetSelection());
		
		new DragFrame(page, page_drop_group, detached_page_handler);
		
		DetachedPageEvent e(page, EVT_PAGE_DETACHED);
		ProcessEvent(e);
	}
	else{
		event.Skip();
	}
}

REHex::DetachableNotebook::DragFrame *REHex::DetachableNotebook::DragFrame::instance = NULL;

BEGIN_EVENT_TABLE(REHex::DetachableNotebook::DragFrame, wxFrame)
#ifdef REHEX_TABDRAGFRAME_FAKE_CAPTURE
	EVT_TIMER(wxID_ANY, REHex::DetachableNotebook::DragFrame::OnMousePoll)
#else
	EVT_MOTION(REHex::DetachableNotebook::DragFrame::OnMotion)
	EVT_MOUSE_CAPTURE_LOST(REHex::DetachableNotebook::DragFrame::OnCaptureLost)
	EVT_LEFT_UP(REHex::DetachableNotebook::DragFrame::OnLeftUp)
#endif
END_EVENT_TABLE()

REHex::DetachableNotebook::DragFrame::DragFrame(wxWindow *page, const void *page_drop_group, wxEvtHandler *detached_page_handler):
	wxFrame(NULL, wxID_ANY, "", wxDefaultPosition, page->GetSize(), (wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP)),
	page_drop_group(page_drop_group),
	detached_page_handler(detached_page_handler),
	page(page),
	dragging(true)
#ifdef REHEX_TABDRAGFRAME_FAKE_CAPTURE
	, mouse_poll_timer(this, wxID_ANY)
#endif
{
	assert(instance == NULL);
	instance = this;
	
	SetTransparent(127);
	
	page->Reparent(this);
	#ifdef __APPLE__
	page->Show();
	#endif
	Show();
	
#ifdef REHEX_TABDRAGFRAME_FAKE_CAPTURE
	mouse_poll_timer.Start(50);
#else
	CallAfter([&]()
	{
		CaptureMouse();
	});
#endif
}

REHex::DetachableNotebook::DragFrame::~DragFrame()
{
	assert(instance == this);
	instance = NULL;
}

REHex::DetachableNotebook::DragFrame *REHex::DetachableNotebook::DragFrame::get_instance()
{
	return instance;
}

static wxAuiTabCtrl *find_wxAuiTabCtrl(wxWindow *w)
{
	auto w_children = w->GetChildren();
	
	for(auto c = w_children.GetFirst(); c; c = c->GetNext())
	{
		wxWindow *cw = (wxWindow*)(c->GetData());
		
		wxAuiTabCtrl *tc = dynamic_cast<wxAuiTabCtrl*>(cw);
		if(tc != NULL)
		{
			return tc;
		}
		
		tc = find_wxAuiTabCtrl(cw);
		if(tc != NULL)
		{
			return tc;
		}
	}
	
	return NULL;
}

void REHex::DetachableNotebook::DragFrame::drop()
{
	wxPoint mouse_pos = wxGetMousePosition();
	
	MainWindow *window = NULL;
	
	const std::list<MainWindow*> &all_windows = MainWindow::get_instances();
	for(auto w = all_windows.begin(); w != all_windows.end(); ++w)
	{
		if((*w)->IsIconized())
		{
			continue;
		}
		
		if((*w)->GetScreenRect().Contains(mouse_pos))
		{
			wxAuiTabCtrl *w_tc = find_wxAuiTabCtrl(*w);
			if(w_tc->GetScreenRect().Contains(mouse_pos))
			{
				window = *w;
			}
			
			break;
		}
	}
	
	if(window == NULL)
	{
		DetachedPageEvent e(page, EVT_PAGE_DROPPED);
		bool e_handled = detached_page_handler->ProcessEvent(e);
		assert(e_handled);
	}
	else{
		window->insert_tab((Tab*)(page), -1);
	}
}

#ifdef REHEX_TABDRAGFRAME_FAKE_CAPTURE
void REHex::DetachableNotebook::DragFrame::OnMousePoll(wxTimerEvent &event)
{
	if(dragging)
	{
		wxMouseState mouse_state = wxGetMouseState();
		
		if(mouse_state.LeftIsDown())
		{
			wxPoint mouse_pos = wxGetMousePosition();
			SetPosition(mouse_pos);
		}
		else{
			mouse_poll_timer.Stop();
			
			dragging = false;
			drop();
			
			Destroy();
		}
	}
}
#else
void REHex::DetachableNotebook::DragFrame::OnMotion(wxMouseEvent &event)
{
	wxPoint mouse_pos = wxGetMousePosition();
	SetPosition(mouse_pos);
}

void REHex::DetachableNotebook::DragFrame::OnCaptureLost(wxMouseCaptureLostEvent &event)
{
	if(dragging)
	{
		dragging = false;
		drop();
	}
	
	Destroy();
}

void REHex::DetachableNotebook::DragFrame::OnLeftUp(wxMouseEvent &event)
{
	if(dragging)
	{
		dragging = false;
		ReleaseMouse();
		drop();
	}
	
	Destroy();
}
#endif

REHex::DetachedPageEvent::DetachedPageEvent(wxWindow *page, wxEventType event):
	wxEvent(wxID_NONE, event),
	page(page)
{
	m_propagationLevel = wxEVENT_PROPAGATE_MAX;
}

wxEvent *REHex::DetachedPageEvent::Clone() const
{
	return new DetachedPageEvent(page, GetEventType());
}
