#pragma once
#include <vector>
#include <functional>
#include <Arduino.h>  // For String type

enum class MenuItemType {
    ReadOnly,
    Selector,
    Range,
    Submenu
};

struct MenuItem {
    String label;
    MenuItemType type;
    std::function<String()> getValue;
    std::function<void(int8_t)> onInput;
    std::function<void()> onSelect;

    MenuItem(const String& l, MenuItemType t,
             std::function<String()> gv = nullptr,
             std::function<void(int8_t)> oi = nullptr,
             std::function<void()> os = nullptr)
        : label(l), type(t), getValue(gv), onInput(oi), onSelect(os) {}
};

struct Menu {
    std::vector<MenuItem> items;
    Menu* parent = nullptr;

    void clear() {
        items.clear();
    }
};