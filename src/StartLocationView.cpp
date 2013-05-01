// Copyright � 2008-2013 Pioneer Developers. See AUTHORS.txt for details
// Licensed under the terms of the GPL v3. See licenses/GPL-3.txt

#include "libs.h"
#include "Factions.h"
#include "GalacticView.h"
#include "Lang.h"
#include "Pi.h"
#include "Game.h"
#include "StartLocationView.h"
#include "StringF.h"
#include "SystemInfoView.h"
#include "galaxy/Sector.h"
#include "galaxy/StarSystem.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Renderer.h"
#include "gui/Gui.h"
#include <algorithm>
#include <sstream>

using namespace Graphics;

#define INNER_RADIUS (Sector::SIZE*1.5f)
#define OUTER_RADIUS (Sector::SIZE*3.0f)
#define FAR_THRESHOLD 5.f
#define FAR_LIMIT     36.f
#define FAR_MAX       46.f

enum DetailSelection {
	DETAILBOX_NONE    = 0
,	DETAILBOX_INFO    = 1
,	DETAILBOX_FACTION = 2
};

static const float ZOOM_SPEED = 15;
static const float WHEEL_SENSITIVITY = .03f;		// Should be a variable in user settings.

StartLocationView::StartLocationView()
{
	InitDefaults();

	m_rotX = m_rotXMovingTo = m_rotXDefault;
	m_rotZ = m_rotZMovingTo = m_rotZDefault;
	m_zoom = m_zoomMovingTo = m_zoomDefault;

	m_current = new SystemPath(0,0,0,0,9); // Earth, Los Angeles
	m_current = m_current.SystemOnly();
	m_selected = m_current;

	GotoSystem(m_current);
	m_pos = m_posMovingTo;

	m_matchTargetToSelection   = true;
	m_selectionFollowsMovement = false;
	m_detailBoxVisible         = DETAILBOX_INFO;
	m_toggledFaction           = false;

	InitObject();
}

void StartLocationView::InitDefaults()
{
	m_rotXDefault = Pi::config->Float("SectorViewXRotation");
	m_rotZDefault = Pi::config->Float("SectorViewZRotation");
	m_zoomDefault = Pi::config->Float("SectorViewZoom");
	m_rotXDefault = Clamp(m_rotXDefault, -170.0f, -10.0f);
	m_zoomDefault = Clamp(m_zoomDefault, 0.1f, 5.0f);
	m_previousSearch = "";

	m_secPosFar = vector3f(INT_MAX, INT_MAX, INT_MAX);
	m_radiusFar = 0;
	m_cacheXMin = 0;
	m_cacheXMax = 0;
	m_cacheYMin = 0;
	m_cacheYMax = 0;
	m_cacheYMin = 0;
	m_cacheYMax = 0;
}

void StartLocationView::InitObject()
{
	SetTransparency(true);

	m_lineVerts.Reset(new Graphics::VertexArray(Graphics::ATTRIB_POSITION | Graphics::ATTRIB_POSITION, 500));

	Gui::Screen::PushFont("OverlayFont");
	m_clickableLabels = new Gui::LabelSet();
	m_clickableLabels->SetLabelColor(Color(.7f,.7f,.7f,0.75f));
	Add(m_clickableLabels, 0, 0);
	Gui::Screen::PopFont();

	m_sectorLabel = new Gui::Label("");
	Add(m_sectorLabel, 2, Gui::Screen::GetHeight()-Gui::Screen::GetFontHeight()*2-66);
	m_distanceLabel = new Gui::Label("");
	Add(m_distanceLabel, 2, Gui::Screen::GetHeight()-Gui::Screen::GetFontHeight()-66);

	m_zoomInButton = new Gui::ImageButton("icons/zoom_in.png");
	m_zoomInButton->SetToolTip(Lang::ZOOM_IN);
	m_zoomInButton->SetRenderDimensions(30, 22);
	Add(m_zoomInButton, 700, 5);

	m_zoomLevelLabel = (new Gui::Label(""))->Color(0.27f, 0.86f, 0.92f);
	Add(m_zoomLevelLabel, 640, 5);

	m_zoomOutButton = new Gui::ImageButton("icons/zoom_out.png");
	m_zoomOutButton->SetToolTip(Lang::ZOOM_OUT);
	m_zoomOutButton->SetRenderDimensions(30, 22);
	Add(m_zoomOutButton, 732, 5);

	Add(new Gui::Label(Lang::SEARCH), 650, 500);
	m_searchBox = new Gui::TextEntry();
	m_searchBox->onKeyPress.connect(sigc::mem_fun(this, &StartLocationView::OnSearchBoxKeyPress));
	Add(m_searchBox, 700, 500);

	m_disk.Reset(new Graphics::Drawables::Disk(Pi::renderer, Color::WHITE, 0.2f));

	m_infoBox = new Gui::VBox();
	m_infoBox->SetTransparency(false);
	m_infoBox->SetBgColor(0.05f, 0.05f, 0.12f, 0.5f);
	m_infoBox->SetSpacing(10.0f);
	Add(m_infoBox, 5, 5);
	
	// 1. return button
	m_menuButton = new Gui::LabelButton(new Gui::Label(Lang::RETURN_TO_MENU));
	m_menuButton->onClick.connect(sigc::bind(sigc::ptr_fun(&Pi::SetView), static_cast<View*>(0)));
	m_infoBox->PackEnd(m_menuButton);

	// 2. holds info about current, selected systems
	Gui::VBox *locationsBox = new Gui::VBox();
	locationsBox->SetSpacing(5.f);
	// 2.1 current system
	Gui::VBox *systemBox = new Gui::VBox();
	Gui::HBox *hbox = new Gui::HBox();
	hbox->SetSpacing(5.0f);
	Gui::Button *b = new Gui::SolidButton();
	b->onClick.connect(sigc::mem_fun(this, &StartLocationView::GotoCurrentSystem));
	hbox->PackEnd(b);
	hbox->PackEnd((new Gui::Label(Lang::CURRENT_SYSTEM))->Color(1.0f, 1.0f, 1.0f));
	systemBox->PackEnd(hbox);
	hbox = new Gui::HBox();
	hbox->SetSpacing(5.0f);
	m_currentSystemLabels.systemName = (new Gui::Label(""))->Color(1.0f, 1.0f, 0.0f);
	m_currentSystemLabels.distance = (new Gui::Label(""))->Color(1.0f, 0.0f, 0.0f);
	hbox->PackEnd(m_currentSystemLabels.systemName);
	hbox->PackEnd(m_currentSystemLabels.distance);
	systemBox->PackEnd(hbox);
	m_currentSystemLabels.starType = (new Gui::Label(""))->Color(1.0f, 0.0f, 1.0f);
	m_currentSystemLabels.shortDesc = (new Gui::Label(""))->Color(1.0f, 0.0f, 1.0f);
	systemBox->PackEnd(m_currentSystemLabels.starType);
	systemBox->PackEnd(m_currentSystemLabels.shortDesc);
	locationsBox->PackEnd(systemBox);
	// 2.2 selected system
	systemBox = new Gui::VBox();
	hbox = new Gui::HBox();
	hbox->SetSpacing(5.0f);
	b = new Gui::SolidButton();
	b->onClick.connect(sigc::mem_fun(this, &StartLocationView::GotoSelectedSystem));
	hbox->PackEnd(b);
	hbox->PackEnd((new Gui::Label(Lang::SELECTED_SYSTEM))->Color(1.0f, 1.0f, 1.0f));
	systemBox->PackEnd(hbox);
	hbox = new Gui::HBox();
	hbox->SetSpacing(5.0f);
	m_selectedSystemLabels.systemName = (new Gui::Label(""))->Color(1.0f, 1.0f, 0.0f);
	m_selectedSystemLabels.distance = (new Gui::Label(""))->Color(1.0f, 0.0f, 0.0f);
	hbox->PackEnd(m_selectedSystemLabels.systemName);
	hbox->PackEnd(m_selectedSystemLabels.distance);
	systemBox->PackEnd(hbox);
	m_selectedSystemLabels.starType = (new Gui::Label(""))->Color(1.0f, 0.0f, 1.0f);
	m_selectedSystemLabels.shortDesc = (new Gui::Label(""))->Color(1.0f, 0.0f, 1.0f);
	systemBox->PackEnd(m_selectedSystemLabels.starType);
	systemBox->PackEnd(m_selectedSystemLabels.shortDesc);
	locationsBox->PackEnd(systemBox);
	m_infoBox->PackEnd(locationsBox);
	
	// 3. holds options for displaying systems
	Gui::VBox *filterBox = new Gui::VBox();
	// 3.1 Draw vertical lines
	hbox = new Gui::HBox();
	hbox->SetSpacing(5.0f);
	m_drawSystemLegButton = (new Gui::ToggleButton());
	m_drawSystemLegButton->SetPressed(false); // TODO: replace with var
	hbox->PackEnd(m_drawSystemLegButton);
	Gui::Label *label = (new Gui::Label(Lang::DRAW_VERTICAL_LINES))->Color(1.0f, 1.0f, 1.0f);
	hbox->PackEnd(label);
	filterBox->PackEnd(hbox);
	m_infoBox->PackEnd(filterBox);

	// 4. start button
	m_startButton = new Gui::LabelButton(new Gui::Label(Lang::RETURN_TO_MENU));
	m_startButton->onClick.connect(sigc::mem_fun(this, &StartLocationView::OnStartButtonClick));
	m_infoBox->PackEnd(m_startButton);

	m_onMouseButtonDown =
		Pi::onMouseButtonDown.connect(sigc::mem_fun(this, &StartLocationView::MouseButtonDown));

	UpdateSystemLabels(m_currentSystemLabels, m_current);
	UpdateSystemLabels(m_selectedSystemLabels, m_selected);

	m_factionBox = new Gui::VBox();
	m_factionBox->SetTransparency(false);
	m_factionBox->SetBgColor(0.05f, 0.05f, 0.12f, 0.5f);
	m_factionBox->SetSpacing(5.0f);
	m_factionBox->HideAll();
	Add(m_factionBox, 5, 5);
}

StartLocationView::~StartLocationView()
{
	m_onMouseButtonDown.disconnect();
	if (m_onKeyPressConnection.connected()) m_onKeyPressConnection.disconnect();
}

void StartLocationView::OnSearchBoxKeyPress(const SDL_keysym *keysym)
{
	//remember the last search text, hotkey: up
	if (m_searchBox->GetText().empty() && keysym->sym == SDLK_UP && !m_previousSearch.empty())
		m_searchBox->SetText(m_previousSearch);

	if (keysym->sym != SDLK_KP_ENTER && keysym->sym != SDLK_RETURN)
		return;

	std::string search = m_searchBox->GetText();
	if (!search.size())
		return;

	m_previousSearch = search;

	//Try to detect if user entered a sector address, comma or space separated, strip parentheses
	//system index is unreliable, so it is not supported
	try {
		GotoSector(SystemPath::Parse(search.c_str()));
		return;
	} catch (SystemPath::ParseFailure) {}

	bool gotMatch = false, gotStartMatch = false;
	SystemPath bestMatch;
	const std::string *bestMatchName = 0;

	for (std::map<SystemPath,Sector*>::iterator i = m_sectorCache.begin(); i != m_sectorCache.end(); ++i)

		for (unsigned int systemIndex = 0; systemIndex < (*i).second->m_systems.size(); systemIndex++) {
			const Sector::System *ss = &((*i).second->m_systems[systemIndex]);

			// compare with the start of the current system
			if (strncasecmp(search.c_str(), ss->name.c_str(), search.size()) == 0) {

				// matched, see if they're the same size
				if (search.size() == ss->name.size()) {

					// exact match, take it and go
					SystemPath path = (*i).first;
					path.systemIndex = systemIndex;
					//Pi::cpan->MsgLog()->Message("", stringf(Lang::EXACT_MATCH_X, formatarg("system", ss->name)));
					GotoSystem(path);
					return;
				}

				// partial match at start of name
				if (!gotMatch || !gotStartMatch || bestMatchName->size() > ss->name.size()) {

					// don't already have one or its shorter than the previous
					// one, take it
					bestMatch = (*i).first;
					bestMatch.systemIndex = systemIndex;
					bestMatchName = &(ss->name);
					gotMatch = gotStartMatch = true;
				}

				continue;
			}

			// look for the search term somewhere within the current system
			if (pi_strcasestr(ss->name.c_str(), search.c_str())) {

				// found it
				if (!gotMatch || !gotStartMatch || bestMatchName->size() > ss->name.size()) {

					// best we've found so far, take it
					bestMatch = (*i).first;
					bestMatch.systemIndex = systemIndex;
					bestMatchName = &(ss->name);
					gotMatch = true;
				}
			}
		}

	if (gotMatch) {
		//Pi::cpan->MsgLog()->Message("", stringf(Lang::NOT_FOUND_BEST_MATCH_X, formatarg("system", *bestMatchName)));
		GotoSystem(bestMatch);
	}
	//else
		//Pi::cpan->MsgLog()->Message("", Lang::NOT_FOUND);
}

#define DRAW_RAD	  3
#define FFRAC(_x)	((_x)-floor(_x))

void StartLocationView::Draw3D()
{
	m_lineVerts->Clear();
	m_clickableLabels->Clear();

	if (m_zoomClamped <= FAR_THRESHOLD) m_renderer->SetPerspectiveProjection(40.f, Pi::GetScrAspect(), 1.f, 100.f);
	else                                m_renderer->SetPerspectiveProjection(40.f, Pi::GetScrAspect(), 1.f, 600.f);

	matrix4x4f modelview = matrix4x4f::Identity();
	m_renderer->ClearScreen();

	m_sectorLabel->SetText(stringf(Lang::SECTOR_X_Y_Z,
		formatarg("x", int(floorf(m_pos.x))),
		formatarg("y", int(floorf(m_pos.y))),
		formatarg("z", int(floorf(m_pos.z)))));

	m_zoomLevelLabel->SetText(stringf(Lang::NUMBER_LY, formatarg("distance", ((m_zoomClamped/FAR_THRESHOLD )*(OUTER_RADIUS)) + 0.5 * Sector::SIZE)));

	vector3f dv = vector3f(floorf(m_pos.x)-m_current.sectorX, floorf(m_pos.y)-m_current.sectorY, floorf(m_pos.z)-m_current.sectorZ) * Sector::SIZE;
	m_distanceLabel->SetText(stringf(Lang::DISTANCE_LY, formatarg("distance", dv.Length())));

	// units are lightyears, my friend
	modelview.Translate(0.f, 0.f, -10.f-10.f*m_zoom);    // not zoomClamped, let us zoom out a bit beyond what we're drawing
	modelview.Rotate(DEG2RAD(m_rotX), 1.f, 0.f, 0.f);
	modelview.Rotate(DEG2RAD(m_rotZ), 0.f, 0.f, 1.f);
	modelview.Translate(-FFRAC(m_pos.x)*Sector::SIZE, -FFRAC(m_pos.y)*Sector::SIZE, -FFRAC(m_pos.z)*Sector::SIZE);
	m_renderer->SetTransform(modelview);

	m_renderer->SetBlendMode(BLEND_ALPHA);

	if (m_zoomClamped <= FAR_THRESHOLD)
		DrawNearSectors(modelview);
	else
		DrawFarSectors(modelview);

	//draw sector legs in one go
	m_renderer->SetTransform(matrix4x4f::Identity());
	if (m_lineVerts->GetNumVerts() > 2)
		m_renderer->DrawLines(m_lineVerts->GetNumVerts(), &m_lineVerts->position[0], &m_lineVerts->diffuse[0]);

	UpdateFactionToggles();

	m_renderer->SetBlendMode(BLEND_SOLID);
}

void StartLocationView::GotoSector(const SystemPath &path)
{
	m_posMovingTo = vector3f(path.sectorX, path.sectorY, path.sectorZ);

	// for performance don't animate the travel if we're Far Zoomed
	if (m_zoomClamped > FAR_THRESHOLD) m_pos = m_posMovingTo;
}

void StartLocationView::GotoSystem(const SystemPath &path)
{
	Sector* ps = GetCached(path.sectorX, path.sectorY, path.sectorZ);
	const vector3f &p = ps->m_systems[path.systemIndex].p;
	m_posMovingTo.x = path.sectorX + p.x/Sector::SIZE;
	m_posMovingTo.y = path.sectorY + p.y/Sector::SIZE;
	m_posMovingTo.z = path.sectorZ + p.z/Sector::SIZE;

	// for performance don't animate the travel if we're Far Zoomed
	if (m_zoomClamped > FAR_THRESHOLD) m_pos = m_posMovingTo;
}

void StartLocationView::SetSelectedSystem(const SystemPath &path)
{
    m_selected = path;
	UpdateSystemLabels(m_selectedSystemLabels, m_selected);
}

void StartLocationView::OnClickSystem(const SystemPath &path)
{
	//if (m_selectionFollowsMovement)
		SetSelectedSystem(path);
		GotoSystem(path);
	//else
		//SetSelectedSystem(path);
}

void StartLocationView::PutSystemLabels(Sector *sec, const vector3f &origin, int drawRadius)
{
	Uint32 sysIdx = 0;
	for (std::vector<Sector::System>::iterator sys = sec->m_systems.begin(); sys !=sec->m_systems.end(); ++sys, ++sysIdx) {
		// skip the system if it doesn't fall within the sphere we're viewing.
		if ((m_pos*Sector::SIZE - (*sys).FullPosition()).Length() > drawRadius) continue;

		// if the system is the current system or selected we can't skip it
		bool can_skip = !sys->IsSameSystem(m_selected) && !sys->IsSameSystem(m_current);

		// skip the system if it belongs to a Faction we've toggled off and we can skip it
		if (m_hiddenFactions.find((*sys).faction) != m_hiddenFactions.end() && can_skip) continue;

		// place the label
		vector3d systemPos = vector3d((*sys).FullPosition() - origin);
		vector3d screenPos;
		if (Gui::Screen::Project(systemPos, screenPos)) {
			// work out the colour
			Color labelColor = (*sys).faction->AdjustedColour((*sys).population, true);

			// get a system path to pass to the event handler when the label is licked
			SystemPath sysPath = SystemPath((*sys).sx, (*sys).sy, (*sys).sz, sysIdx);

			// setup the label;
			m_clickableLabels->Add((*sys).name, sigc::bind(sigc::mem_fun(this, &StartLocationView::OnClickSystem), sysPath), screenPos.x, screenPos.y, labelColor);
		}
	}
}

void StartLocationView::PutFactionLabels(const vector3f &origin)
{
	glDepthRange(0,1);
	Gui::Screen::EnterOrtho();
	for (std::set<Faction*>::iterator it = m_visibleFactions.begin(); it != m_visibleFactions.end(); ++it) {
		if ((*it)->hasHomeworld && m_hiddenFactions.find((*it)) == m_hiddenFactions.end()) {

			Sector::System sys = GetCached((*it)->homeworld)->m_systems[(*it)->homeworld.systemIndex];
			if ((m_pos*Sector::SIZE - sys.FullPosition()).Length() > (m_zoomClamped/FAR_THRESHOLD )*OUTER_RADIUS) continue;

			vector3d pos;
			if (Gui::Screen::Project(vector3d(sys.FullPosition() - origin), pos)) {

				std::string labelText    = sys.name + "\n" + (*it)->name;
				Color       labelColor  = (*it)->colour;
				float       labelHeight = 0;
				float       labelWidth  = 0;

				Gui::Screen::MeasureString(labelText, labelWidth, labelHeight);

				if (!m_material) m_material.Reset(m_renderer->CreateMaterial(Graphics::MaterialDescriptor()));

				{
					Graphics::VertexArray va(Graphics::ATTRIB_POSITION);
					va.Add(vector3f(pos.x - 5.f,              pos.y - 5.f,               0));
					va.Add(vector3f(pos.x - 5.f,              pos.y - 5.f + labelHeight, 0));
					va.Add(vector3f(pos.x + labelWidth + 5.f, pos.y - 5.f,               0));
					va.Add(vector3f(pos.x + labelWidth + 5.f, pos.y - 5.f + labelHeight, 0));
					m_material->diffuse = Color(0.05f, 0.05f, 0.12f, 0.65f);
					m_renderer->DrawTriangles(&va, m_material.Get(), Graphics::TRIANGLE_STRIP);
				}

				{
					Graphics::VertexArray va(Graphics::ATTRIB_POSITION);
					va.Add(vector3f(pos.x - 8.f, pos.y,       0));
					va.Add(vector3f(pos.x      , pos.y + 8.f, 0));
					va.Add(vector3f(pos.x,       pos.y - 8.f, 0));
					va.Add(vector3f(pos.x + 8.f, pos.y,       0));
					m_material->diffuse = labelColor;
					m_renderer->DrawTriangles(&va, m_material.Get(), Graphics::TRIANGLE_STRIP);
				}

				if (labelColor.GetLuminance() > 0.75f) labelColor.a = 0.8f;    // luminance is sometimes a bit overly
				m_clickableLabels->Add(labelText, sigc::bind(sigc::mem_fun(this, &StartLocationView::OnClickSystem), (*it)->homeworld), pos.x, pos.y, labelColor);
			}
		}
	}
	Gui::Screen::LeaveOrtho();
}

void StartLocationView::UpdateSystemLabels(SystemLabels &labels, const SystemPath &path)
{
	Sector *sec = GetCached(path.sectorX, path.sectorY, path.sectorZ);
	Sector *playerSec = GetCached(m_current.sectorX, m_current.sectorY, m_current.sectorZ);
	
	const float dist = Sector::DistanceBetween(sec, path.systemIndex, playerSec, m_current.systemIndex);
	
	char format[256];
	snprintf(format, sizeof(format), "[ %s ]", Lang::NUMBER_LY);
	labels.distance->SetText(stringf(format, formatarg("distance", dist)));
	labels.distance->Color(0.0f, 1.0f, 0.2f);
	m_jumpLine.SetColor(Color(0.f, 1.f, 0.2f, 1.f));

	RefCountedPtr<StarSystem> sys = StarSystem::GetCached(path);

	std::string desc;
	if (sys->GetNumStars() == 4) {
		desc = Lang::QUADRUPLE_SYSTEM;
	} else if (sys->GetNumStars() == 3) {
		desc = Lang::TRIPLE_SYSTEM;
	} else if (sys->GetNumStars() == 2) {
		desc = Lang::BINARY_SYSTEM;
	} else {
		desc = sys->rootBody->GetAstroDescription();
	}
	labels.starType->SetText(desc);

	labels.systemName->SetText(sys->GetName());
	labels.shortDesc->SetText(sys->GetShortDescription());

	if (m_detailBoxVisible == DETAILBOX_INFO) m_infoBox->ShowAll();
}

void StartLocationView::OnToggleFaction(Gui::ToggleButton* button, bool pressed, Faction* faction)
{
	// hide or show the faction's systems depending on whether the button is pressed
	if (pressed) m_hiddenFactions.erase(faction);
	else         m_hiddenFactions.insert(faction);

	m_toggledFaction = true;
}

void StartLocationView::UpdateFactionToggles()
{
	// make sure we have enough row in the ui
	while (m_visibleFactionLabels.size() < m_visibleFactions.size()) {
		Gui::HBox*         row    = new Gui::HBox();
		Gui::ToggleButton* toggle = new Gui::ToggleButton();
		Gui::Label*        label  = new Gui::Label("");

		toggle->SetToolTip("");
		label ->SetToolTip("");

		m_visibleFactionToggles.push_back(toggle);
		m_visibleFactionLabels.push_back(label);
		m_visibleFactionRows.push_back(row);

		row->SetSpacing(5.0f);
		row->PackEnd(toggle);
		row->PackEnd(label);
		m_factionBox->PackEnd(row);
	}

	// set up the faction labels, and the toggle buttons
	Uint32 rowIdx = 0;
	for (std::set<Faction*>::iterator it = m_visibleFactions.begin(); it != m_visibleFactions.end(); ++it, ++rowIdx) {
		m_visibleFactionLabels [rowIdx]->SetText((*it)->name);
		m_visibleFactionLabels [rowIdx]->Color((*it)->colour);
		m_visibleFactionToggles[rowIdx]->onChange.clear();
		m_visibleFactionToggles[rowIdx]->SetPressed(m_hiddenFactions.find((*it)) == m_hiddenFactions.end());
		m_visibleFactionToggles[rowIdx]->onChange.connect(sigc::bind(sigc::mem_fun(this, &StartLocationView::OnToggleFaction),*it));
		m_visibleFactionRows   [rowIdx]->ShowAll();
	}

	// hide any rows, and disconnect any toggle event handler, that we're not using
	for (; rowIdx < m_visibleFactionLabels.size(); rowIdx++) {
		m_visibleFactionToggles[rowIdx]->onChange.clear();
		m_visibleFactionRows   [rowIdx]->Hide();
	}

	if  (m_detailBoxVisible == DETAILBOX_FACTION) m_factionBox->Show();
	else                                          m_factionBox->HideAll();
}

void StartLocationView::DrawNearSectors(matrix4x4f modelview)
{
	m_visibleFactions.clear();

	Sector *playerSec = GetCached(m_current.sectorX, m_current.sectorY, m_current.sectorZ);
	vector3f playerPos = Sector::SIZE * vector3f(float(m_current.sectorX), float(m_current.sectorY), float(m_current.sectorZ)) + playerSec->m_systems[m_current.systemIndex].p;

	for (int sx = -DRAW_RAD; sx <= DRAW_RAD; sx++) {
		for (int sy = -DRAW_RAD; sy <= DRAW_RAD; sy++) {
			for (int sz = -DRAW_RAD; sz <= DRAW_RAD; sz++) {
				DrawNearSector(int(floorf(m_pos.x))+sx, int(floorf(m_pos.y))+sy, int(floorf(m_pos.z))+sz, playerPos,
					modelview * matrix4x4f::Translation(Sector::SIZE*sx, Sector::SIZE*sy, Sector::SIZE*sz));
			}
		}
	}

	// ...then switch and do all the labels
	const vector3f secOrigin = vector3f(int(floorf(m_pos.x)), int(floorf(m_pos.y)), int(floorf(m_pos.z)));

	m_renderer->SetTransform(modelview);
	glDepthRange(0,1);
	Gui::Screen::EnterOrtho();
	for (int sx = -DRAW_RAD; sx <= DRAW_RAD; sx++) {
		for (int sy = -DRAW_RAD; sy <= DRAW_RAD; sy++) {
			for (int sz = -DRAW_RAD; sz <= DRAW_RAD; sz++) {
				PutSystemLabels(GetCached(sx + secOrigin.x, sy + secOrigin.y, sz + secOrigin.z), Sector::SIZE * secOrigin, Sector::SIZE * DRAW_RAD);
			}
		}
	}
	Gui::Screen::LeaveOrtho();
}

void StartLocationView::DrawNearSector(int sx, int sy, int sz, const vector3f &playerAbsPos,const matrix4x4f &trans)
{
	m_renderer->SetTransform(trans);
	Sector* ps = GetCached(sx, sy, sz);

	int cz = int(floor(m_pos.z+0.5f));

	if (cz == sz) {
		const Color darkgreen(0.f, 0.2f, 0.f, 1.f);
		const vector3f vts[] = {
			vector3f(0.f, 0.f, 0.f),
			vector3f(0.f, Sector::SIZE, 0.f),
			vector3f(Sector::SIZE, Sector::SIZE, 0.f),
			vector3f(Sector::SIZE, 0.f, 0.f)
		};

		m_renderer->DrawLines(4, vts, darkgreen, LINE_LOOP);
	}

	Uint32 sysIdx = 0;
	for (std::vector<Sector::System>::iterator i = ps->m_systems.begin(); i != ps->m_systems.end(); ++i, ++sysIdx) {
		// calculate where the system is in relation the centre of the view...
		const vector3f sysAbsPos = Sector::SIZE*vector3f(float(sx), float(sy), float(sz)) + (*i).p;
		const vector3f toCentreOfView = m_pos*Sector::SIZE - sysAbsPos;

		// ...and skip the system if it doesn't fall within the sphere we're viewing.
		if (toCentreOfView.Length() > OUTER_RADIUS) continue;

		// if the system is the current system or selected we can't skip it
		bool can_skip = !i->IsSameSystem(m_selected) && !i->IsSameSystem(m_current);

		// if the system belongs to a faction we've chosen to temporarily hide
		// then skip it if we can
		m_visibleFactions.insert(i->faction);
		if (m_hiddenFactions.find(i->faction) != m_hiddenFactions.end() && can_skip) continue;

		// don't worry about looking for inhabited systems if they're
		// unexplored (same calculation as in StarSystem.cpp) or we've
		// already retrieved their population.
		if ((*i).population < 0 && isqrt(1 + sx*sx + sy*sy + sz*sz) <= 90) {

			// only do this once we've pretty much stopped moving.
			vector3f diff = vector3f(
					fabs(m_posMovingTo.x - m_pos.x),
					fabs(m_posMovingTo.y - m_pos.y),
					fabs(m_posMovingTo.z - m_pos.z));

			// Ideally, since this takes so f'ing long, it wants to be done as a threaded job but haven't written that yet.
			if( (diff.x < 0.001f && diff.y < 0.001f && diff.z < 0.001f) ) {
				SystemPath current = SystemPath(sx, sy, sz, sysIdx);
				RefCountedPtr<StarSystem> pSS = StarSystem::GetCached(current);
				(*i).population = pSS->GetTotalPop();
			}

		}

		matrix4x4f systrans = trans * matrix4x4f::Translation((*i).p.x, (*i).p.y, (*i).p.z);
		m_renderer->SetTransform(systrans);

		if (m_drawSystemLegButton->GetPressed() || !can_skip) {
			const Color light(0.5f);
			const Color dark(0.2f);

			// draw system "leg"
			float z = -(*i).p.z;
			if (sz <= cz)
				z = z+abs(cz-sz)*Sector::SIZE;
			else
				z = z-abs(cz-sz)*Sector::SIZE;
			m_lineVerts->Add(systrans * vector3f(0.f, 0.f, z), light);
			m_lineVerts->Add(systrans * vector3f(0.f, 0.f, z * 0.5f), dark);
			m_lineVerts->Add(systrans * vector3f(0.f, 0.f, z * 0.5f), dark);
			m_lineVerts->Add(systrans * vector3f(0.f, 0.f, 0.f), light);

			//cross at other end
			m_lineVerts->Add(systrans * vector3f(-0.1f, -0.1f, z), light);
			m_lineVerts->Add(systrans * vector3f(0.1f, 0.1f, z), light);
			m_lineVerts->Add(systrans * vector3f(-0.1f, 0.1f, z), light);
			m_lineVerts->Add(systrans * vector3f(0.1f, -0.1f, z), light);
		}

		if (i->IsSameSystem(m_selected)) {
			m_jumpLine.SetStart(vector3f(0.f, 0.f, 0.f));
			m_jumpLine.SetEnd(playerAbsPos - sysAbsPos);
			m_jumpLine.Draw(m_renderer);
		}

		// draw star blob itself
		systrans.Rotate(DEG2RAD(-m_rotZ), 0, 0, 1);
		systrans.Rotate(DEG2RAD(-m_rotX), 1, 0, 0);
		systrans.Scale((StarSystem::starScale[(*i).starType[0]]));
		m_renderer->SetTransform(systrans);

		float *col = StarSystem::starColors[(*i).starType[0]];
		m_disk->SetColor(Color(col[0], col[1], col[2]));
		m_disk->Draw(m_renderer);

		// current location indicator
		if (i->IsSameSystem(m_current)) {
			glDepthRange(0.2,1.0);
			m_disk->SetColor(Color(0.f, 0.f, 0.8f));
			m_renderer->SetTransform(systrans * matrix4x4f::ScaleMatrix(3.f));
			m_disk->Draw(m_renderer);
		}
		// selected location indicator
		if (i->IsSameSystem(m_current)) {
			glDepthRange(0.1,1.0);
			m_disk->SetColor(Color(0.f, 0.8f, 0.f));
			m_renderer->SetTransform(systrans * matrix4x4f::ScaleMatrix(2.f));
			m_disk->Draw(m_renderer);
		}
	}
}

void StartLocationView::DrawFarSectors(matrix4x4f modelview)
{
	int buildRadius = ceilf((m_zoomClamped/FAR_THRESHOLD) * 3);
	if (buildRadius <= DRAW_RAD) buildRadius = DRAW_RAD;

	const vector3f secOrigin = vector3f(int(floorf(m_pos.x)), int(floorf(m_pos.y)), int(floorf(m_pos.z)));

	// build vertex and colour arrays for all the stars we want to see, if we don't already have them
	if (m_toggledFaction || buildRadius != m_radiusFar || !secOrigin.ExactlyEqual(m_secPosFar)) {
		m_farstars       .clear();
		m_farstarsColor  .clear();
		m_visibleFactions.clear();

		for (int sx = secOrigin.x-buildRadius; sx <= secOrigin.x+buildRadius; sx++) {
			for (int sy = secOrigin.y-buildRadius; sy <= secOrigin.y+buildRadius; sy++) {
				for (int sz = secOrigin.z-buildRadius; sz <= secOrigin.z+buildRadius; sz++) {
						if ((vector3f(sx,sy,sz) - secOrigin).Length() <= buildRadius){
							BuildFarSector(GetCached(sx, sy, sz), Sector::SIZE * secOrigin, m_farstars, m_farstarsColor);
						}
					}
				}
			}

		m_secPosFar      = secOrigin;
		m_radiusFar      = buildRadius;
		m_toggledFaction = false;
	}

	// always draw the stars, slightly altering their size for different different resolutions, so they still look okay
	if (m_farstars.size() > 0)
		m_renderer->DrawPoints(m_farstars.size(), &m_farstars[0], &m_farstarsColor[0], 1.f + (Graphics::GetScreenHeight() / 720.f));

	// also add labels for any faction homeworlds among the systems we've drawn
	PutFactionLabels(Sector::SIZE * secOrigin);
}

void StartLocationView::BuildFarSector(Sector* sec, const vector3f &origin, std::vector<vector3f> &points, std::vector<Color> &colors)
{
	Color starColor;
	for (std::vector<Sector::System>::iterator i = sec->m_systems.begin(); i != sec->m_systems.end(); ++i) {
		// skip the system if it doesn't fall within the sphere we're viewing.
		if ((m_pos*Sector::SIZE - (*i).FullPosition()).Length() > (m_zoomClamped/FAR_THRESHOLD )*OUTER_RADIUS) continue;

		// if the system belongs to a faction we've chosen to hide also skip it, if it's not selectd in some way
		m_visibleFactions.insert(i->faction);
		if (m_hiddenFactions.find(i->faction) != m_hiddenFactions.end()
			&& !i->IsSameSystem(m_selected) && !i->IsSameSystem(m_current)) continue;

		// otherwise add the system's position (origin must be m_pos's *sector* or we get judder)
		// and faction color to the list to draw
		starColor = (*i).faction->colour;
		starColor.a = .75f;

		points.push_back((*i).FullPosition() - origin);
		colors.push_back(starColor);
	}
}

void StartLocationView::OnSwitchTo() {
	if (!m_onKeyPressConnection.connected())
		m_onKeyPressConnection =
			Pi::onKeyPress.connect(sigc::mem_fun(this, &StartLocationView::OnKeyPressed));

	Update();

	UpdateSystemLabels(m_selectedSystemLabels, m_selected);
}

void StartLocationView::RefreshDetailBoxVisibility()
{
	if (m_detailBoxVisible != DETAILBOX_INFO)    m_infoBox->HideAll();    else m_infoBox->ShowAll();
	if (m_detailBoxVisible != DETAILBOX_FACTION) m_factionBox->HideAll(); else UpdateFactionToggles();
}

void StartLocationView::OnKeyPressed(SDL_keysym *keysym)
{
	if (Pi::GetView() != this) {
		m_onKeyPressConnection.disconnect();
		return;
	}

	// XXX ugly hack checking for Lua console here
	if (Pi::IsConsoleActive())
		return;

	// ignore keypresses if they're typing
	if (m_searchBox->IsFocused()) {
		// but if they press enter then we want future keys
		if (keysym->sym == SDLK_KP_ENTER || keysym->sym == SDLK_RETURN)
			m_searchBox->Unfocus();
		return;
	}

	// '/' focuses the search box
	if (keysym->sym == SDLK_KP_DIVIDE || keysym->sym == SDLK_SLASH) {
		m_searchBox->SetText("");
		m_searchBox->GrabFocus();
		return;
	}

	// cycle through the info box, the faction box, and nothing
	if (keysym->sym == SDLK_TAB) {
		if (m_detailBoxVisible == DETAILBOX_FACTION) m_detailBoxVisible = DETAILBOX_NONE;
		else                                         m_detailBoxVisible++;
		RefreshDetailBoxVisibility();
		return;
	}

	// toggle selection mode
	if (keysym->sym == SDLK_KP_ENTER || keysym->sym == SDLK_RETURN) {
		m_selectionFollowsMovement = !m_selectionFollowsMovement;
		/*if (m_selectionFollowsMovement)
			Pi::cpan->MsgLog()->Message("", Lang::ENABLED_AUTOMATIC_SYSTEM_SELECTION);
		else
			Pi::cpan->MsgLog()->Message("", Lang::DISABLED_AUTOMATIC_SYSTEM_SELECTION);*/
		return;
	}

	// fast move selection to current or selected system
	if (keysym->sym == SDLK_c || keysym->sym == SDLK_g) {
		if (keysym->sym == SDLK_c)
			GotoSystem(m_current);
		else if (keysym->sym == SDLK_g)
			GotoSystem(m_selected);

		if (Pi::KeyState(SDLK_LSHIFT) || Pi::KeyState(SDLK_RSHIFT)) {
			while (m_rotZ < -180.0f) m_rotZ += 360.0f;
			while (m_rotZ > 180.0f)  m_rotZ -= 360.0f;
			m_rotXMovingTo = m_rotXDefault;
			m_rotZMovingTo = m_rotZDefault;
			m_zoomMovingTo = m_zoomDefault;
		}
		return;
	}

	// reset rotation and zoom
	if (keysym->sym == SDLK_r) {
		while (m_rotZ < -180.0f) m_rotZ += 360.0f;
		while (m_rotZ > 180.0f)  m_rotZ -= 360.0f;
		m_rotXMovingTo = m_rotXDefault;
		m_rotZMovingTo = m_rotZDefault;
		m_zoomMovingTo = m_zoomDefault;
		return;
	}
}

void StartLocationView::Update()
{
	const float frameTime = Pi::GetFrameTime();

	matrix4x4f rot = matrix4x4f::Identity();
	rot.RotateX(DEG2RAD(-m_rotX));
	rot.RotateZ(DEG2RAD(-m_rotZ));

	// don't check raw keypresses if the search box is active
	// XXX ugly hack checking for Lua console here
	if (!m_searchBox->IsFocused() && !Pi::IsConsoleActive()) {
		const float moveSpeed = Pi::GetMoveSpeedShiftModifier();
		float move = moveSpeed*frameTime;
		if (Pi::KeyState(SDLK_LEFT) || Pi::KeyState(SDLK_RIGHT))
			m_posMovingTo += vector3f(Pi::KeyState(SDLK_LEFT) ? -move : move, 0,0) * rot;
		if (Pi::KeyState(SDLK_UP) || Pi::KeyState(SDLK_DOWN))
			m_posMovingTo += vector3f(0, Pi::KeyState(SDLK_DOWN) ? -move : move, 0) * rot;
		if (Pi::KeyState(SDLK_PAGEUP) || Pi::KeyState(SDLK_PAGEDOWN))
			m_posMovingTo += vector3f(0,0, Pi::KeyState(SDLK_PAGEUP) ? -move : move) * rot;

		if (Pi::KeyState(SDLK_EQUALS)) m_zoomMovingTo -= move;
		if (Pi::KeyState(SDLK_MINUS)) m_zoomMovingTo += move;
		if (m_zoomInButton->IsPressed()) m_zoomMovingTo -= move;
		if (m_zoomOutButton->IsPressed()) m_zoomMovingTo += move;
		m_zoomMovingTo = Clamp(m_zoomMovingTo, 0.1f, FAR_MAX);

		if (Pi::KeyState(SDLK_a) || Pi::KeyState(SDLK_d))
			m_rotZMovingTo += (Pi::KeyState(SDLK_a) ? -0.5f : 0.5f) * moveSpeed;
		if (Pi::KeyState(SDLK_w) || Pi::KeyState(SDLK_s))
			m_rotXMovingTo += (Pi::KeyState(SDLK_w) ? -0.5f : 0.5f) * moveSpeed;
	}

	if (Pi::MouseButtonState(SDL_BUTTON_RIGHT)) {
		int motion[2];
		Pi::GetMouseMotion(motion);

		m_rotXMovingTo += 0.2f*float(motion[1]);
		m_rotZMovingTo += 0.2f*float(motion[0]);
	}

	m_rotXMovingTo = Clamp(m_rotXMovingTo, -170.0f, -10.0f);

	{
		vector3f diffPos = m_posMovingTo - m_pos;
		vector3f travelPos = diffPos * 10.0f*frameTime;
		if (travelPos.Length() > diffPos.Length()) m_pos = m_posMovingTo;
		else m_pos = m_pos + travelPos;

		float diffX = m_rotXMovingTo - m_rotX;
		float travelX = diffX * 10.0f*frameTime;
		if (fabs(travelX) > fabs(diffX)) m_rotX = m_rotXMovingTo;
		else m_rotX = m_rotX + travelX;

		float diffZ = m_rotZMovingTo - m_rotZ;
		float travelZ = diffZ * 10.0f*frameTime;
		if (fabs(travelZ) > fabs(diffZ)) m_rotZ = m_rotZMovingTo;
		else m_rotZ = m_rotZ + travelZ;

		float prevZoom = m_zoom;
		float diffZoom = m_zoomMovingTo - m_zoom;
		float travelZoom = diffZoom * ZOOM_SPEED*frameTime;
		if (fabs(travelZoom) > fabs(diffZoom)) m_zoom = m_zoomMovingTo;
		else m_zoom = m_zoom + travelZoom;
		m_zoomClamped = Clamp(m_zoom, 1.f, FAR_LIMIT);

		// swtich between Info and Faction panels when we zoom over the threshold
		if (m_zoom <= FAR_THRESHOLD && prevZoom > FAR_THRESHOLD && m_detailBoxVisible == DETAILBOX_FACTION) {
			m_detailBoxVisible = DETAILBOX_INFO;
			RefreshDetailBoxVisibility();
		}
		if (m_zoom > FAR_THRESHOLD && prevZoom <= FAR_THRESHOLD && m_detailBoxVisible == DETAILBOX_INFO) {
			m_detailBoxVisible = DETAILBOX_FACTION;
			RefreshDetailBoxVisibility();
		}
	}

	if (m_selectionFollowsMovement) {
		SystemPath new_selected = SystemPath(int(floor(m_pos.x)), int(floor(m_pos.y)), int(floor(m_pos.z)), 0);

		Sector* ps = GetCached(new_selected.sectorX, new_selected.sectorY, new_selected.sectorZ);
		if (ps->m_systems.size()) {
			float px = FFRAC(m_pos.x)*Sector::SIZE;
			float py = FFRAC(m_pos.y)*Sector::SIZE;
			float pz = FFRAC(m_pos.z)*Sector::SIZE;

			float min_dist = FLT_MAX;
			for (unsigned int i=0; i<ps->m_systems.size(); i++) {
				Sector::System *ss = &ps->m_systems[i];
				float dx = px - ss->p.x;
				float dy = py - ss->p.y;
				float dz = pz - ss->p.z;
				float dist = sqrtf(dx*dx + dy*dy + dz*dz);
				if (dist < min_dist) {
					min_dist = dist;
					new_selected.systemIndex = i;
				}
			}

			if (m_selected != new_selected)
				SetSelectedSystem(new_selected);
		}
	}

	ShrinkCache();
}

void StartLocationView::ShowAll()
{
	View::ShowAll();
	if (m_detailBoxVisible != DETAILBOX_INFO)    m_infoBox->HideAll();
	if (m_detailBoxVisible != DETAILBOX_FACTION) m_factionBox->HideAll();
}

void StartLocationView::MouseButtonDown(int button, int x, int y)
{
	if (this == Pi::GetView()) {
		if (Pi::MouseButtonState(SDL_BUTTON_WHEELDOWN))
			m_zoomMovingTo += ZOOM_SPEED * WHEEL_SENSITIVITY * Pi::GetMoveSpeedShiftModifier();
		else if (Pi::MouseButtonState(SDL_BUTTON_WHEELUP))
			m_zoomMovingTo -= ZOOM_SPEED * WHEEL_SENSITIVITY * Pi::GetMoveSpeedShiftModifier();
	}
}

Sector* StartLocationView::GetCached(const SystemPath& loc)
{
	Sector *s = 0;

	std::map<SystemPath,Sector*>::iterator i = m_sectorCache.find(loc);
	if (i != m_sectorCache.end())
		return (*i).second;

	s = new Sector(loc.sectorX, loc.sectorY, loc.sectorZ);
	m_sectorCache.insert( std::pair<SystemPath,Sector*>(loc, s) );
	s->AssignFactions();

	return s;
}

Sector* StartLocationView::GetCached(const int sectorX, const int sectorY, const int sectorZ)
{
	const SystemPath loc(sectorX, sectorY, sectorZ);
	return GetCached(loc);
}

void StartLocationView::ShrinkCache()
{
	// we're going to use these to determine if our sectors are within the range that we'll ever render
	int drawRadius = ceilf((m_zoomClamped/FAR_THRESHOLD) * 3);
	if (m_zoomClamped <= FAR_THRESHOLD) drawRadius = DRAW_RAD;

	const int xmin = int(floorf(m_pos.x))-drawRadius;
	const int xmax = int(floorf(m_pos.x))+drawRadius;
	const int ymin = int(floorf(m_pos.y))-drawRadius;
	const int ymax = int(floorf(m_pos.y))+drawRadius;
	const int zmin = int(floorf(m_pos.z))-drawRadius;
	const int zmax = int(floorf(m_pos.z))+drawRadius;

	// XXX don't clear the current/selected/target sectors

	if  (xmin != m_cacheXMin || xmax != m_cacheXMax
	  || ymin != m_cacheYMin || ymax != m_cacheYMax
	  || zmin != m_cacheZMin || zmax != m_cacheZMax) {
		std::map<SystemPath,Sector*>::iterator iter = m_sectorCache.begin();
		while (iter != m_sectorCache.end())	{
			Sector *s = (*iter).second;
			//check_point_in_box
			if (s && !s->WithinBox( xmin, xmax, ymin, ymax, zmin, zmax )) {
				delete s;
				m_sectorCache.erase( iter++ );
			} else {
				iter++;
			}
		}

		m_cacheXMin = xmin;
		m_cacheXMax = xmax;
		m_cacheYMin = ymin;
		m_cacheYMax = ymax;
		m_cacheZMin = zmin;
		m_cacheZMax = zmax;
	}
}

void StartLocationView::OnStartButtonClick() {
	if (!Pi::game) {
		SystemPath *path = new SystemPath(m_selected); // selected star system
		RefCountedPtr<StarSystem> system(StarSystem::GetCached(path));
		path->bodyIndex = 0; // for single star systems this is a star
		if (system->GetNumStars() > 1) {
			// for multistar systems bodyIndex=0 is a gravpoint;
			// we can't start game at gravpoint (it's not a body)
			// so let's start at first star
			path->bodyIndex = 1; // for multistar systems it is guaranteed to exist
		}
		SystemBody *sbody = system->GetBodyByPath(*path);
		Pi::game = new Game(*path, vector3d(0, 4*sbody->GetRadius(), 0));
	}
}
