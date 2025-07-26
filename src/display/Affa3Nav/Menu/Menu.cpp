#include "Menu.h"
#include <cstdio>

MenuItem::MenuItem(MenuItemType t, const char* l)
    : type(t), label(l) {}

Menu::Menu(const char* h)
    : header(h) {}

void Menu::navigateUp() {
    if (selectedIndex > 0)
        selectedIndex--;
    else
        selectedIndex = (int)items.size() - 1;
}

void Menu::navigateDown() {
    if (selectedIndex < (int)items.size() - 1)
        selectedIndex++;
    else
        selectedIndex = 0;
}

std::string Menu::getItemString(int index) const {
    if (index < 0 || index >= (int)items.size()) return "";

    const MenuItem& mi = items[index];
    char buf[64];
    switch (mi.type) {
        case MenuItemType::StaticText:
            snprintf(buf, sizeof(buf), "%s: %s", mi.label, mi.staticValue ? mi.staticValue : "");
            return std::string(buf);

        case MenuItemType::OptionSelector:
            if (mi.options.empty()) return std::string(mi.label) + ":";
            return std::string(mi.label) + ": " + mi.options[mi.selectedOption];

        case MenuItemType::IntegerEditor:
            snprintf(buf, sizeof(buf), "%s: %d", mi.label, mi.intValue);
            return std::string(buf);

        case MenuItemType::SubMenu:
            return std::string(mi.label) + " >";

        default:
            return "";
    }
}

uint8_t Menu::getScrollIndicator() const {
    // 0x0B means both arrows visible
    // bit0 = up arrow, bit1 = down arrow
    // We'll set 0x0B default, and clear bits if no scroll possible

    uint8_t indicator = 0x0B;

    if (selectedIndex == 0)
        indicator &= ~0x01;  // Clear up arrow
    if (selectedIndex == (int)items.size() - 1)
        indicator &= ~0x02;  // Clear down arrow

    return indicator;
}