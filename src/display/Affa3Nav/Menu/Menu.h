#pragma once
#include <Arduino.h>
#include <vector>  
#include "../../AffaDisplayBase.h"
// enum class MenuItemType {
//     StaticText,
//     OptionSelector,
//     IntegerEditor,
//     SubMenu,
// };


// ========================
// Editable field type
// ========================
enum class FieldType {
    Integer, // Min/Max/Step
    List     // Predefined list of strings
};
struct Field {
    FieldType type;

    // For integer fields
    int intValue = 0;
    int minValue = 0;
    int maxValue = 100;
    int step = 1;
    String unit = ""; // e.g. "%"

    // For list fields
    std::vector<String> list;
    int listIndex = 0;

    // Callback when value changes
    std::function<void(Field&)> onChange;
};
struct MenuViewBox {
    String header;
    String line1;
    String line2;
    uint8_t scrollIndicator;
    uint8_t selectedRow;
};

// ========================
// Menu item type
// ========================
struct MenuItem {
    String label;                 // Name (e.g. "Brightness")
    std::vector<Field> fields;    // Editable subfields
    bool editable = true;         // false → read-only
    String fieldSeparator = " "; // Separator between fields
}; 
struct Menu {
public:
        //TODO:create IMENU class and inherit from it
    Menu(String tittle) : header(tittle){};

    void addItem(const MenuItem& item);
    void show(); // Display menu
    void HighlightCurrentSelection(int row);

    void handleKey(AffaCommon::AffaKey key, bool isHold); // everything passes through this

    bool isActive() const; 

    const MenuViewBox& getCurrentViewBox() const { return currentViewBox; }

private:
    void updateViewBox();
  
    //Affa3NavDisplay &display;  // reference to display
    String header;         // Menu header
    std::vector<MenuItem> items;
    int selectedIndex = 0;   // Which item is selected
    int selectedRow = 0; // Which row is selected in the current window
    int editingFieldIndex = 0; // Which subfield is being edited
    bool editing = false;
    MenuViewBox currentViewBox;

    bool active = false;
    // Navigation
    void selectNext();
    void selectPrev();

    void enterEditMode();
    void exitEditMode();
    void nextFieldOrExit();
    void editFieldValue(int delta);
    void HighlightSelection(int row);
    void printMenuToSerial(String header, MenuItem item1, MenuItem item2, uint8_t scrollLockIndicator, uint8_t selectedRow) ;
    //void displayItem(const MenuItem& item, bool selected, bool editing);
    // Helpers to get display strings
    // String getItemString(MenuItem item) const;
    String getItemString(const MenuItem &item) const;

    // // Helper for clamping values
    // void clampValue(MenuItem &item);

    // Get scroll lock indicator flags for menu (0x0B means arrows visible)
    uint8_t getScrollIndicator();
     
};
