#pragma once
#include <vector>
#include <string>

enum class MenuItemType {
    StaticText,
    OptionSelector,
    IntegerEditor,
    SubMenu,
};

struct Menu;

struct MenuItem {
    MenuItemType type;
    const char* label;

    // For StaticText
    const char* staticValue = nullptr;

    // For OptionSelector
    std::vector<const char*> options;
    int selectedOption = 0;

    // For IntegerEditor
    int intValue = 0;
    int minValue = 0;
    int maxValue = 100;

    // For SubMenu
    Menu* submenu = nullptr;

    MenuItem(MenuItemType t, const char* l);
};

struct Menu {
    const char* header = nullptr;
    std::vector<MenuItem> items;
    int selectedIndex = 0;
    Menu* parent = nullptr;

    Menu(const char* h);

    void navigateUp();
    void navigateDown();
    void draw();

    // Helpers to get display strings
    std::string getItemString(int index) const;

    // Get scroll lock indicator flags for menu (0x0B means arrows visible)
    uint8_t getScrollIndicator() const;
};