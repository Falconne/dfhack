#include "uicommon.h"

#include <functional>

// DF data structure definition headers
#include "DataDefs.h"
#include "Types.h"
#include "df/ui.h"
#include "df/ui_build_selector.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

#include "modules/Gui.h"
#include "modules/Maps.h"

#include "modules/World.h"

#include "tinydir/tinydir.h"

using df::global::ui;
using df::global::ui_build_selector;
using df::global::world;

DFHACK_PLUGIN("qfhack");
#define PLUGIN_VERSION 0.1

struct coord32_t
{
    int32_t x, y, z;

    df::coord get_coord16() const
    {
        return df::coord(x, y, z);
    }
};

DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
    return CR_OK;
}

#define SIDEBAR_WIDTH 30

static string sep = "/";

struct FileEntry
{
    string path, name;
    bool isDir, upDir;
};

static bool isTopLevel(string pwd)
{
    if (pwd.empty())
        return true;
    auto end = pwd.length();
    return (pwd[end-1] == sep[0]);
}


class ViewscreenFileManager : public dfhack_viewscreen
{
public:
    ViewscreenFileManager()
    {
        selected_column = 0;
        files_column.setTitle("File");
        files_column.multiselect = false;
        files_column.allow_search = true;
        files_column.left_margin = 2;

        files_column.changeHighlight(0);

        populateFiles();

        files_column.selectDefaultEntry();
    }

    void feed(set<df::interface_key> *input)
    {
        bool key_processed = false;
        switch (selected_column)
        {
        case 0:
            key_processed = files_column.feed(input);
            if (input->count(interface_key::SELECT))
            {
                FileEntry *entry = files_column.getFirstSelectedElem();
                if (entry)
                {
                    if (entry->upDir)
                    {
                        auto r = pwd.find_last_of(sep);
                        pwd = pwd.substr(0, r);
                        auto n = std::count(pwd.begin(), pwd.end(), sep[0]);
                        if (n == 0)
                            pwd += sep;
                        populateFiles();
                    }
                    else if (entry->isDir)
                    {
                        pwd = entry->path;
                        populateFiles();
                    }
                }

            }
            break;
        }

        if (key_processed)
            return;

        if (input->count(interface_key::LEAVESCREEN))
        {
            input->clear();
            Screen::dismiss(this);
            return;
        }
        else if  (input->count(interface_key::SELECT))
        {
            Screen::dismiss(this);
        }
        else if  (input->count(interface_key::CURSOR_LEFT))
        {
            --selected_column;
            validateColumn();
        }
        else if  (input->count(interface_key::CURSOR_RIGHT))
        {
            selected_column++;
            validateColumn();
        }
        else if (enabler->tracking_on && enabler->mouse_lbut)
        {
            if (files_column.setHighlightByMouse())
                selected_column = 0;

            enabler->mouse_lbut = enabler->mouse_rbut = 0;
        }
    }

    void render()
    {
        if (Screen::isDismissed(this))
            return;

        dfhack_viewscreen::render();

        Screen::clear();
        Screen::drawBorder("Dir: " + pwd);

        files_column.display(selected_column == 0);
        int32_t y = gps->dimy - 3;
        int32_t x = 2;
        OutputHotkeyString(x, y, "Leave", "Esc");
        x += 3;
        OutputHotkeyString(x, y, "Select", "Enter");
    }

    std::string getFocusString() { return "qfhack_choosemat"; }

    static void initialize()
    {
#ifdef _WINDOWS
        pwd = "C:\\";
        sep = "\\";
#else
        pwd = "/home";
        sep = "/";
#endif
    }

private:
    vector<FileEntry> file_entries;
    ListColumn<FileEntry *> files_column;
    int selected_column;
    static string pwd;

    void populateFiles()
    {
        files_column.clear();
        file_entries.clear();

        if (!isTopLevel(pwd))
        {
            FileEntry entry;
            entry.upDir = true;
            entry.name = "<UP_DIR>";
            file_entries.push_back(entry);
        }

        tinydir_dir dir;
        tinydir_open(&dir, pwd.c_str());
        while (dir.has_next)
        {
            tinydir_file file;
            tinydir_readfile(&dir, &file);

            string name(file.name);

            if (name == "." || name == "..")
            {
                tinydir_next(&dir);
                continue;
            }

            string path = pwd;
            if (!isTopLevel(pwd))
                path += sep;
            path += name;

            FileEntry entry;
            entry.upDir = false;
            if (file.is_dir)
            {
                entry.isDir = true;
                name += sep;
            }
            else
            {
                entry.isDir = false;
                auto r = path.find_last_of(".");
                auto ext = path.substr(r+1);
                if (toLower(ext) != "csv")
                {
                    tinydir_next(&dir);
                    continue;
                }
            }
            entry.path = path;
            entry.name = name;

            file_entries.push_back(entry);
            tinydir_next(&dir);
        }
        tinydir_close(&dir);

        for (auto it = file_entries.begin(); it != file_entries.end(); it++)
        {
            files_column.add(it->name, &*it);
        }

        files_column.clearSearch();
        files_column.filterDisplay();
        files_column.setHighlight(0);
    }

    void validateColumn()
    {
        set_to_limit(selected_column, 1);
    }

    void resize(int32_t x, int32_t y)
    {
        dfhack_viewscreen::resize(x, y);
        files_column.resize();
    }
};

string ViewscreenFileManager::pwd = "";

bool qf_ui_enabled = false;

//START Viewscreen Hook
struct qfhack_hook : public df::viewscreen_dwarfmodest
{
    //START UI Methods
    typedef df::viewscreen_dwarfmodest interpose_base;

    void send_key(const df::interface_key &key)
    {
        set< df::interface_key > keys;
        keys.insert(key);
        this->feed(&keys);
    }

    bool showQFui()
    {
        auto dims = Gui::getDwarfmodeViewDims();
        return (ui->main.mode == ui_sidebar_mode::Default && 
            qf_ui_enabled &&
            dims.menu_x1 > 0);
    }

    bool handleInput(set<df::interface_key> *input)
    {
        if (!showQFui())
            return false;

        if (input->count(interface_key::CUSTOM_X))
        {
            qf_ui_enabled = false;
        }
        else if (input->count(interface_key::CUSTOM_L))
        {
            Screen::show(new ViewscreenFileManager());
        }
        else
        {
            return false;
        }

        return true;
    }

    DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *input))
    {
        if (!handleInput(input))
            INTERPOSE_NEXT(feed)(input);
    }

    DEFINE_VMETHOD_INTERPOSE(void, render, ())
    {
        INTERPOSE_NEXT(render)();

        if (!showQFui())
            return;

        auto dims = Gui::getDwarfmodeViewDims();

        Screen::Pen pen(' ', COLOR_BLACK);
        int left_margin = dims.menu_x1 + 1;
        int x = left_margin;
        int y = dims.y1 + 1;
        Screen::fillRect(pen, x, y, dims.menu_x2, y + 20);

        OutputString(COLOR_BROWN, x, y, "Quickfort Interface", true, left_margin);
        OutputHotkeyString(x, y, "Load File", "l", true, left_margin);
        OutputHotkeyString(x, y, "Cancel", "x", true, left_margin);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(qfhack_hook, feed);
IMPLEMENT_VMETHOD_INTERPOSE(qfhack_hook, render);


static command_result qfhack_cmd(color_ostream &out, vector <string> & parameters)
{
    if (!parameters.empty())
    {
        if (parameters.size() == 1 && toLower(parameters[0])[0] == 'v')
        {
            out << "QuickFort DFHack" << endl << "Version: " << PLUGIN_VERSION << endl;
        }
        else
        {
            return CR_WRONG_USAGE;
        }
    }
    else
    {
        //Screen::show(new ViewscreenFileManager());
        qf_ui_enabled = true;
    }

    return CR_OK;
}


DFhackCExport command_result plugin_init(color_ostream &out, std::vector <PluginCommand> &commands)
{
    if (!gps || !INTERPOSE_HOOK(qfhack_hook, feed).apply() || !INTERPOSE_HOOK(qfhack_hook, render).apply())
        out.printerr("Could not insert QuickFort dfhack hooks\n");

    commands.push_back(
        PluginCommand(
        "qfhack", "",
        qfhack_cmd, false, ""));

    ViewscreenFileManager::initialize();
    qf_ui_enabled = false;

    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event)
{
    switch (event) {
    case SC_MAP_LOADED:
        break;
    default:
        break;
    }

    return CR_OK;
}
