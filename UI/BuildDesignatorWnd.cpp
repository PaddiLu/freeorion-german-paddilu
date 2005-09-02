#include "BuildDesignatorWnd.h"

#include "../util/AppInterface.h"
#include "../universe/Building.h"
#include "GGDrawUtil.h"
#include "GGStaticGraphic.h"
#include "../universe/Effect.h"
#include "../client/human/HumanClientApp.h"
#include "GGLayout.h"
#include "MapWnd.h"
#include "../util/MultiplayerCommon.h"
#include "../universe/ShipDesign.h"
#include "SidePanel.h"
#include "TechWnd.h"

#include <boost/format.hpp>

namespace {
    const int DEFENSE_BASE_BUILD_TURNS = 10; // this is a kludge for v0.3 only
    const int DEFENSE_BASE_BUILD_COST = 20; // this is a kludge for v0.3 only
}

//////////////////////////////////////////////////
// BuildDesignatorWnd::BuildDetailPanel
//////////////////////////////////////////////////
class BuildDesignatorWnd::BuildDetailPanel : public GG::Wnd
{
public:
    BuildDetailPanel(int w, int h);
    virtual bool Render();
    void SelectedBuildLocation(int location);
    void SetBuildItem(BuildType build_type, const std::string& item);
    //void SetBuild(BuildType build_type, const std::string& item, ...);
    void Reset();
    mutable boost::signal<void (BuildType, const std::string&)> CenterOnBuildSignal;
    mutable boost::signal<void (BuildType, const std::string&)> RequestBuildItemSignal;

private:
    GG::Pt ItemGraphicUpperLeft() const;
    void CenterClickedSlot();
    void AddToQueueClickedSlot();
    void CheckBuildability();

    BuildType           m_build_type;
    std::string         m_item;
    int                 m_build_location;
    GG::TextControl*    m_item_name_text;
    GG::TextControl*    m_cost_text;
    CUIButton*          m_recenter_button;
    CUIButton*          m_add_to_queue_button;
    CUIMultiEdit*       m_description_box;
    GG::StaticGraphic*  m_item_graphic;
};

BuildDesignatorWnd::BuildDetailPanel::BuildDetailPanel(int w, int h) :
    Wnd(0, 0, w, h, GG::Wnd::CLICKABLE),
    m_build_type(INVALID_BUILD_TYPE),
    m_item(""),
    m_build_location(UniverseObject::INVALID_OBJECT_ID)
{
    const int NAME_PTS = ClientUI::PTS + 8;
    const int CATEGORY_AND_TYPE_PTS = ClientUI::PTS + 4;
    const int COST_PTS = ClientUI::PTS;
    const int BUTTON_WIDTH = 150;
    const int BUTTON_MARGIN = 5;
    m_item_name_text = new GG::TextControl(1, 0, w - 1 - BUTTON_WIDTH, NAME_PTS + 4, "", ClientUI::FONT_BOLD, NAME_PTS, ClientUI::TEXT_COLOR);
    m_cost_text = new GG::TextControl(1, m_item_name_text->LowerRight().y, w - 1 - BUTTON_WIDTH, COST_PTS + 4, "", ClientUI::FONT, COST_PTS, ClientUI::TEXT_COLOR);
    m_add_to_queue_button = new CUIButton(w - 1 - BUTTON_WIDTH, 1, BUTTON_WIDTH, UserString("PRODUCTION_DETAIL_ADD_TO_QUEUE"));
    m_recenter_button = new CUIButton(w - 1 - BUTTON_WIDTH, m_add_to_queue_button->LowerRight().y + BUTTON_MARGIN, BUTTON_WIDTH, UserString("PRODUCTION_DETAIL_CENTER_ON_BUILD"));
    m_recenter_button->Hide();
    m_add_to_queue_button->Hide();
    m_recenter_button->Disable();
    m_add_to_queue_button->Disable();
    m_description_box = new CUIMultiEdit(1, m_cost_text->LowerRight().y, w - 2 - BUTTON_WIDTH, h - m_cost_text->LowerRight().y - 2, "", GG::TF_WORDBREAK | GG::MultiEdit::READ_ONLY);
    m_description_box->SetColor(GG::CLR_ZERO);
    m_description_box->SetInteriorColor(GG::CLR_ZERO);

    m_item_graphic = 0;

    GG::Connect(m_recenter_button->ClickedSignal, &BuildDesignatorWnd::BuildDetailPanel::CenterClickedSlot, this);
    GG::Connect(m_add_to_queue_button->ClickedSignal, &BuildDesignatorWnd::BuildDetailPanel::AddToQueueClickedSlot, this);

    AttachChild(m_item_name_text);
    AttachChild(m_cost_text);
    AttachChild(m_recenter_button);
    AttachChild(m_add_to_queue_button);
    AttachChild(m_description_box);
}

bool BuildDesignatorWnd::BuildDetailPanel::Render()
{
    GG::Pt ul = UpperLeft(), lr = LowerRight();
    GG::FlatRectangle(ul.x, ul.y, lr.x, lr.y, ClientUI::WND_COLOR, GG::CLR_ZERO, 0);
    return true;
}

void BuildDesignatorWnd::BuildDetailPanel::SelectedBuildLocation(int location)
{
    m_build_location = location;
    CheckBuildability();
}

void BuildDesignatorWnd::BuildDetailPanel::SetBuildItem(BuildType build_type, const std::string& item)
{
    m_build_type = build_type;
    m_item = item;
    Reset();
}

void BuildDesignatorWnd::BuildDetailPanel::Reset()
{
    m_item_name_text->SetText("");
    m_cost_text->SetText("");
    m_description_box->SetText("");

    if (m_item_graphic) {
        DeleteChild(m_item_graphic);
        m_item_graphic = 0;
    }

    if (m_build_type == INVALID_BUILD_TYPE) {
        m_recenter_button->Hide();
        m_add_to_queue_button->Hide();
        m_recenter_button->Disable();
        m_add_to_queue_button->Disable();
        return;
    }

    m_recenter_button->Show();
    m_add_to_queue_button->Show();
    //m_recenter_button->Disable(false); // TODO: enable conditioned upon the type of details being shown

    Empire* empire = HumanClientApp::Empires().Lookup(HumanClientApp::GetApp()->EmpireID());
    if (!empire)
        return;

    CheckBuildability();

    using boost::io::str;
    using boost::format;
    double cost_per_turn = 0;
    int turns = 0;
    std::string item_name_str = UserString(m_item);
    std::string description_str;
    boost::shared_ptr<GG::Texture> graphic;
    if (m_build_type == BT_BUILDING) {
        assert(empire);
        const BuildingType* building_type = empire->GetBuildingType(m_item);
        assert(building_type);
        turns = building_type->BuildTime();
        boost::tie(cost_per_turn, turns) = empire->ProductionCostAndTime(BT_BUILDING, m_item);
        if (building_type->Effects().empty()) {
            description_str = str(format(UserString("TECH_DETAIL_BUILDING_DESCRIPTION_STR"))
                                  % UserString(building_type->Description()));
        } else {
            description_str = str(format(UserString("PRODUCTION_DETAIL_BUILDING_DESCRIPTION_STR_WITH_EFFECTS"))
                                  % UserString(building_type->Description())
                                  % EffectsDescription(building_type->Effects()));
        }
        if (!building_type->Graphic().empty())
            graphic = HumanClientApp::GetApp()->GetTextureOrDefault(ClientUI::ART_DIR + building_type->Graphic());
    } else if (m_build_type == BT_SHIP) {
        assert(empire);
        const ShipDesign* design = empire->GetShipDesign(m_item);
        assert(design);
        turns = 5; // this is a kludge for v0.3 only
        boost::tie(cost_per_turn, turns) = empire->ProductionCostAndTime(BT_SHIP, m_item);
        item_name_str = m_item;
        description_str = str(format(UserString("PRODUCTION_DETAIL_SHIP_DESCRIPTION_STR"))
                              % design->description
                              % design->attack
                              % design->defense
                              % design->speed);
        graphic = HumanClientApp::GetApp()->GetTextureOrDefault(ClientUI::ART_DIR + design->graphic);
    } else if (m_build_type == BT_ORBITAL) {
        turns = DEFENSE_BASE_BUILD_TURNS;
        cost_per_turn = DEFENSE_BASE_BUILD_COST;
        item_name_str = UserString("DEFENSE_BASE");
        description_str = UserString("DEFENSE_BASE_DESCRIPTION");
        graphic = HumanClientApp::GetApp()->GetTextureOrDefault(ClientUI::ART_DIR + "misc/base1.png"); // this is a kludge for v0.3 only
    }

    if (graphic) {
        GG::Pt ul = ItemGraphicUpperLeft();
        m_item_graphic = new GG::StaticGraphic(ul.x, ul.y, 128, 128, graphic, GG::GR_FITGRAPHIC | GG::GR_PROPSCALE);
        m_item_graphic->Show();
        AttachChild(m_item_graphic);
    }

    m_item_name_text->SetText(item_name_str);
    m_cost_text->SetText(str(format(UserString("PRODUCTION_TOTAL_COST_STR"))
                             % static_cast<int>(cost_per_turn + 0.5)
                             % turns));
    m_description_box->SetText(description_str);
}

GG::Pt BuildDesignatorWnd::BuildDetailPanel::ItemGraphicUpperLeft() const
{
    return GG::Pt(Width() - 2 - 150 + (150 - 128) / 2, m_recenter_button->LowerRight().y - UpperLeft().y + 5);
}

void BuildDesignatorWnd::BuildDetailPanel::CenterClickedSlot()
{
    if (m_build_type != INVALID_BUILD_TYPE)
        CenterOnBuildSignal(m_build_type, m_item);
}

void BuildDesignatorWnd::BuildDetailPanel::AddToQueueClickedSlot()
{
    if (m_build_type != INVALID_BUILD_TYPE)
        RequestBuildItemSignal(m_build_type, m_item);
}

void BuildDesignatorWnd::BuildDetailPanel::CheckBuildability()
{
    m_add_to_queue_button->Disable(true);
    Empire* empire = HumanClientApp::Empires().Lookup(HumanClientApp::GetApp()->EmpireID());
    UniverseObject* object = GetUniverse().Object(m_build_location);
    if (m_build_type != INVALID_BUILD_TYPE &&
        empire && object && object->Owners().size() == 1 &&
        object->Owners().find(empire->EmpireID()) != object->Owners().end()) {
        // TODO: after v0.3, check for shipyards, etc.
        m_add_to_queue_button->Disable(false);
    }
}


//////////////////////////////////////////////////
// BuildDesignatorWnd::BuildSelector
//////////////////////////////////////////////////
class BuildDesignatorWnd::BuildSelector : public CUI_Wnd
{
public:
    BuildSelector(int w, int h);

    virtual void MinimizeClicked();

    void Reset();

    mutable boost::signal<void (BuildType, const std::string&)> DisplayBuildItemSignal;
    mutable boost::signal<void (BuildType, const std::string&)> RequestBuildItemSignal;

private:
    struct CategoryClickedFunctor
    {
        CategoryClickedFunctor(BuildType build_type_, BuildSelector& build_selector_);
        void operator()();
        const BuildType build_type;
        BuildSelector&  build_selector;
    };

    void PopulateList(BuildType build_type);
    void BuildItemSelected(const std::set<int>& selections);
    void BuildItemDoubleClicked(int row_index, const boost::shared_ptr<GG::ListBox::Row>& row);

    BuildType               m_current_build_type;
    std::vector<CUIButton*> m_build_category_buttons;
    CUIListBox*             m_buildable_items;
    GG::Pt                  m_original_ul;

    friend struct PopulateListFunctor;
};

BuildDesignatorWnd::BuildSelector::CategoryClickedFunctor::CategoryClickedFunctor(BuildType build_type_, BuildSelector& build_selector_) :
    build_type(build_type_),
    build_selector(build_selector_)
{}

void BuildDesignatorWnd::BuildSelector::CategoryClickedFunctor::operator()()
{
    build_selector.PopulateList(build_type);
}

BuildDesignatorWnd::BuildSelector::BuildSelector(int w, int h) :
    CUI_Wnd(UserString("PRODUCTION_WND_BUILD_ITEMS_TITLE"), 0, 0, w, h, GG::Wnd::CLICKABLE | CUI_Wnd::MINIMIZABLE),
    m_current_build_type(BT_BUILDING)
{
    GG::Pt client_size(w - BORDER_LEFT - BORDER_RIGHT, h - BORDER_TOP - BORDER_BOTTOM);
    GG::Layout* layout = new GG::Layout(BORDER_LEFT, BORDER_TOP, client_size.x, client_size.y, 1, 1, 3, 5);
    const int NUM_CATEGORY_BUTTONS = NUM_BUILD_TYPES - (BT_NOT_BUILDING + 1);
    int button_height;
    for (BuildType i = BuildType(BT_NOT_BUILDING + 1); i < NUM_BUILD_TYPES; i = BuildType(i + 1)) {
        CUIButton* button = new CUIButton(0, 0, 1, UserString("PRODUCTION_WND_CATEGORY_" + boost::lexical_cast<std::string>(i)));
        button_height = button->Height();
        GG::Connect(button->ClickedSignal, CategoryClickedFunctor(i, *this));
        m_build_category_buttons.push_back(button);
        layout->Add(button, 0, i - (BT_NOT_BUILDING + 1));
    }
    m_buildable_items = new CUIListBox(0, 0, 1, 1);
    GG::Connect(m_buildable_items->SelChangedSignal, &BuildDesignatorWnd::BuildSelector::BuildItemSelected, this);
    GG::Connect(m_buildable_items->DoubleClickedSignal, &BuildDesignatorWnd::BuildSelector::BuildItemDoubleClicked, this);
    m_buildable_items->SetStyle(GG::LB_NOSORT | GG::LB_SINGLESEL);
    layout->Add(m_buildable_items, 1, 0, 1, layout->Columns());
    layout->SetMinimumRowHeight(0, button_height);
    layout->SetRowStretch(0, 0);
    layout->SetRowStretch(1, 1);
    AttachChild(layout);
    PopulateList(m_current_build_type);
}

void BuildDesignatorWnd::BuildSelector::MinimizeClicked()
{
    if (!m_minimized) {
        m_original_size = Size();
        m_original_ul = UpperLeft() - (Parent() ? Parent()->ClientUpperLeft() : GG::Pt());
        GG::Pt original_lr = m_original_ul + m_original_size;
        GG::Pt new_size(Width(), BORDER_TOP);
        SetMinSize(new_size.x, new_size.y);
        SizeMove(original_lr - new_size, original_lr);
        if (m_close_button)
            m_close_button->MoveTo(Width() - BUTTON_RIGHT_OFFSET, BUTTON_TOP_OFFSET);
        if (m_minimize_button)
            m_minimize_button->MoveTo(Width() - BUTTON_RIGHT_OFFSET * (m_close_button ? 2 : 1), BUTTON_TOP_OFFSET);
        Hide();
        Show(false);
        if (m_close_button)
            m_close_button->Show();
        if (m_minimize_button)
            m_minimize_button->Show();
        m_minimized = true;
    } else {
        SetMinSize(GG::Pt(Width(), BORDER_TOP + INNER_BORDER_ANGLE_OFFSET + BORDER_BOTTOM));
        SizeMove(m_original_ul, m_original_ul + m_original_size);
        if (m_close_button)
            m_close_button->MoveTo(Width() - BUTTON_RIGHT_OFFSET, BUTTON_TOP_OFFSET);
        if (m_minimize_button)
            m_minimize_button->MoveTo(Width() - BUTTON_RIGHT_OFFSET * (m_close_button ? 2 : 1), BUTTON_TOP_OFFSET);
        Show();
        m_minimized = false;
    }
}

void BuildDesignatorWnd::BuildSelector::Reset()
{
    m_current_build_type = BT_BUILDING;
    PopulateList(m_current_build_type);
}

void BuildDesignatorWnd::BuildSelector::PopulateList(BuildType build_type)
{
    m_current_build_type = build_type;
    m_buildable_items->Clear();
    Empire* empire = HumanClientApp::Empires().Lookup(HumanClientApp::GetApp()->EmpireID());
    if (!empire)
        return;
    if (build_type == BT_BUILDING) {
        Empire::BuildingTypeItr end_it = empire->BuildingTypeEnd();
        for (Empire::BuildingTypeItr it = empire->BuildingTypeBegin(); it != end_it; ++it) {
            GG::ListBox::Row* row = new GG::ListBox::Row();
            row->data_type = *it;
            row->push_back(UserString(*it), ClientUI::FONT, ClientUI::PTS, ClientUI::TEXT_COLOR);
            m_buildable_items->Insert(row);
        }
    } else if (build_type == BT_SHIP) {
        Empire::ShipDesignItr end_it = empire->ShipDesignEnd();
        for (Empire::ShipDesignItr it = empire->ShipDesignBegin(); it != end_it; ++it) {
            GG::ListBox::Row* row = new GG::ListBox::Row();
            row->data_type = it->first;
            row->push_back(it->first, ClientUI::FONT, ClientUI::PTS, ClientUI::TEXT_COLOR);
            m_buildable_items->Insert(row);
        }
    } else if (build_type == BT_ORBITAL) {
        GG::ListBox::Row* row = new GG::ListBox::Row();
        row->data_type = "DEFENSE_BASE";
        row->push_back(UserString(row->data_type), ClientUI::FONT, ClientUI::PTS, ClientUI::TEXT_COLOR);
        m_buildable_items->Insert(row);
    }
}

void BuildDesignatorWnd::BuildSelector::BuildItemSelected(const std::set<int>& selections)
{
    assert(selections.size() == 1);
    DisplayBuildItemSignal(m_current_build_type, m_buildable_items->GetRow(*selections.begin()).data_type);
}

void BuildDesignatorWnd::BuildSelector::BuildItemDoubleClicked(int row_index, const boost::shared_ptr<GG::ListBox::Row>& row)
{
    RequestBuildItemSignal(m_current_build_type, row->data_type);
}


//////////////////////////////////////////////////
// BuildDesignatorWnd
//////////////////////////////////////////////////
BuildDesignatorWnd::BuildDesignatorWnd(int w, int h) :
    Wnd(0, 0, w, h, GG::Wnd::CLICKABLE),
    m_build_location(UniverseObject::INVALID_OBJECT_ID)
{
    const int SIDE_PANEL_PLANET_RADIUS = SidePanel::MAX_PLANET_DIAMETER / 2;
    int CHILD_WIDTHS = w - MapWnd::SIDE_PANEL_WIDTH - SIDE_PANEL_PLANET_RADIUS;
    const int DETAIL_PANEL_HEIGHT = TechTreeWnd::NAVIGATOR_AND_DETAIL_HEIGHT;
    const int BUILD_SELECTOR_HEIGHT = DETAIL_PANEL_HEIGHT;
    m_build_detail_panel = new BuildDetailPanel(CHILD_WIDTHS, DETAIL_PANEL_HEIGHT);
    m_build_selector = new BuildSelector(CHILD_WIDTHS, BUILD_SELECTOR_HEIGHT);
    m_build_selector->MoveTo(0, h - BUILD_SELECTOR_HEIGHT);
    m_side_panel = new SidePanel(CHILD_WIDTHS + SIDE_PANEL_PLANET_RADIUS, 0, MapWnd::SIDE_PANEL_WIDTH, h);
    m_side_panel->HiliteSelectedPlanet(true);
    m_side_panel->Hide();

    GG::Connect(m_build_detail_panel->RequestBuildItemSignal, &BuildDesignatorWnd::BuildItemRequested, this);
    GG::Connect(m_build_selector->DisplayBuildItemSignal, &BuildDesignatorWnd::BuildDetailPanel::SetBuildItem, m_build_detail_panel);
    GG::Connect(m_build_selector->RequestBuildItemSignal, &BuildDesignatorWnd::BuildItemRequested, this);
    GG::Connect(m_side_panel->PlanetSelectedSignal, &BuildDesignatorWnd::SelectPlanet, this);

    m_map_view_hole = GG::Rect(0, 0, CHILD_WIDTHS + SIDE_PANEL_PLANET_RADIUS, h);

    AttachChild(m_build_detail_panel);
    AttachChild(m_build_selector);
    AttachChild(m_side_panel);
}

bool BuildDesignatorWnd::InWindow(const GG::Pt& pt) const
{
    GG::Rect clip_rect = m_map_view_hole + UpperLeft();
    return clip_rect.Contains(pt) ?
        (m_build_detail_panel->InWindow(pt) || m_build_selector->InWindow(pt) || m_side_panel->InWindow(pt)) :
        Wnd::InClient(pt);
}

bool BuildDesignatorWnd::InClient(const GG::Pt& pt) const
{
    GG::Rect clip_rect = m_map_view_hole + UpperLeft();
    return clip_rect.Contains(pt) ?
        (m_build_detail_panel->InClient(pt) || m_build_selector->InClient(pt) || m_side_panel->InClient(pt)) :
        Wnd::InClient(pt);
}

GG::Rect BuildDesignatorWnd::MapViewHole() const
{
    return m_map_view_hole;
}

void BuildDesignatorWnd::CenterOnBuild(/* TODO */)
{
    // TODO
}

void BuildDesignatorWnd::SelectSystem(int system)
{
    if (system != UniverseObject::INVALID_OBJECT_ID && system != m_side_panel->SystemID()) {
        m_side_panel->Show();
        m_side_panel->SetSystem(system);
        m_build_location = UniverseObject::INVALID_OBJECT_ID;
    }
}

void BuildDesignatorWnd::SelectPlanet(int planet)
{
    m_build_location = planet;
    m_build_detail_panel->SelectedBuildLocation(planet);
}

void BuildDesignatorWnd::Reset()
{
    int planet_id = m_side_panel->PlanetID();
    m_side_panel->SetSystem(m_side_panel->SystemID());
    m_side_panel->SelectPlanet(planet_id);
}

void BuildDesignatorWnd::Clear()
{
    m_build_detail_panel->Reset();
    m_build_selector->Reset();
    m_side_panel->SetSystem(UniverseObject::INVALID_OBJECT_ID);
    m_build_location = UniverseObject::INVALID_OBJECT_ID;
}

void BuildDesignatorWnd::BuildItemRequested(BuildType build_type, const std::string& item)
{
    if (m_build_location != UniverseObject::INVALID_OBJECT_ID)
        AddBuildToQueueSignal(build_type, item, 1, m_build_location);
}
