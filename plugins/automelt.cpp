#include "uicommon.h"

#include "modules/Gui.h"

#include "df/world.h"
#include "df/world_raws.h"
#include "df/building_def.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/building_stockpilest.h"
#include "modules/Items.h"
#include "df/ui.h"
#include "modules/Maps.h"
#include "modules/World.h"

using df::global::world;
using df::global::cursor;
using df::global::ui;
using df::building_stockpilest;

DFHACK_PLUGIN("automelt");
#define PLUGIN_VERSION 0.1

static const string PERSISTENCE_KEY = "automelt/stockpiles";


static void mark_all_in_stockpiles(vector<PersistentStockpileInfo> &stockpiles, bool announce)
{
    std::vector<df::item*> &items = world->items.other[items_other_id::IN_PLAY];

    // Precompute a bitmask with the bad flags
    df::item_flags bad_flags;
    bad_flags.whole = 0;

#define F(x) bad_flags.bits.x = true;
    F(dump); F(forbid); F(garbage_collect);
    F(hostile); F(on_fire); F(rotten); F(trader);
    F(in_building); F(construction); F(artifact);
    F(spider_web); F(owned); F(in_job);
#undef F

    size_t marked_count = 0;
    for (size_t i = 0; i < items.size(); i++)
    {
        df::item *item = items[i];
        if (item->flags.whole & bad_flags.whole)
            continue;

        for (auto it = stockpiles.begin(); it != stockpiles.end(); it++)
        {
            if (!is_metal_item(item) || !it->inStockpile(item))
                continue;

            if (item->flags.bits.melt)
                continue;

            ++marked_count;
            world->items.other[items_other_id::ANY_MELT_DESIGNATED].push_back(item);
            item->flags.bits.melt = true;
        }
    }

    if (marked_count)
        Gui::showAnnouncement("Marked " + int_to_string(marked_count) + " items to melt", COLOR_GREEN, false);
    else if (announce)
        Gui::showAnnouncement("No more items to mark", COLOR_RED, true);
}


/*
 * Stockpile Monitoring
 */

class StockpileMonitor
{
public:
    bool isMonitored(df::building_stockpilest *sp)
    {
        for (auto it = monitored_stockpiles.begin(); it != monitored_stockpiles.end(); it++)
        {
            if (it->matches(sp))
                return true;
        }

        return false;
    }

    void add(df::building_stockpilest *sp)
    {
        auto pile = PersistentStockpileInfo(sp, PERSISTENCE_KEY);
        if (pile.isValid())
        {
            monitored_stockpiles.push_back(PersistentStockpileInfo(pile));
            monitored_stockpiles.back().save();
        }
    }

    void remove(df::building_stockpilest *sp)
    {
        for (auto it = monitored_stockpiles.begin(); it != monitored_stockpiles.end(); it++)
        {
            if (it->matches(sp))
            {
                it->remove();
                monitored_stockpiles.erase(it);
                break;
            }
        }
    }

    void doCycle()
    {
        for (auto it = monitored_stockpiles.begin(); it != monitored_stockpiles.end();)
        {
            if (!it->isValid())
                it = monitored_stockpiles.erase(it);
            else
                ++it;
        }

        mark_all_in_stockpiles(monitored_stockpiles, false);
    }

    void reset()
    {
        monitored_stockpiles.clear();
        std::vector<PersistentDataItem> items;
        DFHack::World::GetPersistentData(&items, PERSISTENCE_KEY);

        for (auto i = items.begin(); i != items.end(); i++)
        {
            auto pile = PersistentStockpileInfo(*i, PERSISTENCE_KEY);
            if (pile.load())
                monitored_stockpiles.push_back(PersistentStockpileInfo(pile));
            else
                pile.remove();
        }
    }


private:
    vector<PersistentStockpileInfo> monitored_stockpiles;
};

static StockpileMonitor monitor;

#define DELTA_TICKS 610

DFhackCExport command_result plugin_onupdate ( color_ostream &out )
{
    if(!Maps::IsValid())
        return CR_OK;

    static decltype(world->frame_counter) last_frame_count = 0;

    if (DFHack::World::ReadPauseState())
        return CR_OK;

    if (world->frame_counter - last_frame_count < DELTA_TICKS)
        return CR_OK;

    last_frame_count = world->frame_counter;

    monitor.doCycle();

    return CR_OK;
}


/*
 * Interface
 */

struct melt_hook : public df::viewscreen_dwarfmodest
{
    typedef df::viewscreen_dwarfmodest interpose_base;

    bool handleInput(set<df::interface_key> *input)
    {
        building_stockpilest *sp = get_selected_stockpile();
        if (!sp)
            return false;

        /*if (input->count(interface_key::CUSTOM_M))
        {
            vector<PersistentStockpileInfo> wrapper;
            wrapper.push_back(PersistentStockpileInfo(sp, PERSISTENCE_KEY));
            mark_all_in_stockpiles(wrapper, true);

            return true;
        }
        else*/ if (input->count(interface_key::CUSTOM_SHIFT_M))
        {
            if (monitor.isMonitored(sp))
                monitor.remove(sp);
            else
                monitor.add(sp);
        }

        return false;
    }

    DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *input))
    {
        if (!handleInput(input))
            INTERPOSE_NEXT(feed)(input);
    }

    DEFINE_VMETHOD_INTERPOSE(void, render, ())
    {
        INTERPOSE_NEXT(render)();

        building_stockpilest *sp = get_selected_stockpile();
        if (!sp)
            return;

        auto dims = Gui::getDwarfmodeViewDims();
        int left_margin = dims.menu_x1 + 1;
        int x = left_margin;
        int y = 26;

        OutputToggleString(x, y, "Auto melt", "Shift-M", monitor.isMonitored(sp), true, left_margin);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(melt_hook, feed);
IMPLEMENT_VMETHOD_INTERPOSE(melt_hook, render);


static command_result automelt_cmd(color_ostream &out, vector <string> & parameters)
{
    if (!parameters.empty())
    {
        if (parameters.size() == 1 && toLower(parameters[0])[0] == 'v')
        {
            out << "Building Plan" << endl << "Version: " << PLUGIN_VERSION << endl;
        }
    }

    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event)
{
    switch (event)
    {
    case DFHack::SC_MAP_LOADED:
        monitor.reset();
        break;
    case DFHack::SC_MAP_UNLOADED:
        break;
    default:
        break;
    }
    return CR_OK;
}

DFhackCExport command_result plugin_init ( color_ostream &out, std::vector <PluginCommand> &commands)
{
    if (!gps || !INTERPOSE_HOOK(melt_hook, feed).apply() || !INTERPOSE_HOOK(melt_hook, render).apply())
        out.printerr("Could not insert automelt hooks!\n");

    commands.push_back(
        PluginCommand(
        "automelt", "Automatically flag metal items in marked stockpiles for melting.",
        automelt_cmd, false, ""));

    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
    return CR_OK;
}
