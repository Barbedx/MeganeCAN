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
    int stepMultiplier = 2;
    String unit = ""; // e.g. "%"

    // For list fields
    std::vector<String> list;
    int listIndex = 0;

    // Integer field
    Field(int value, int min, int max, int step = 1,int stepMultiplier =1, const String& unit = "")
        : type(FieldType::Integer),
          intValue(value),
          minValue(min),
          maxValue(max),
          step(step),
          stepMultiplier(stepMultiplier),
          unit(unit) {}

    // Integer field
    Field(int value, const String& unit = "")//readonly
        : type(FieldType::Integer),
          intValue(value),
          minValue(0),
          maxValue(0),
          step(0),
          stepMultiplier(0),
          unit(unit) {}

    // List field (start at given index)
    Field(const std::vector<String>& values, int index = 0)
        : type(FieldType::List),
          list(values),
          listIndex(index) {}

    // Empty/default field
    // Field() : type(FieldType::Int) {}
    // Callback when value changes
    std::function<void(Field&)> onChange;//to store result somewhere?
};
// struct MenuViewBox {
//     String header;
//     String line1;
//     String line2;
//     uint8_t scrollIndicator;
//     uint8_t selectedRow;
// };

// ========================
// Menu item type
// ========================
struct MenuItem {
    String label;                 // Name (e.g. "Brightness")
    std::vector<Field> fields;    // Editable subfields
    std::function<void(const MenuItem&)> onChange; // fires if any field changes

    bool editable = true;         // false → read-only
    String fieldSeparator = " "; // Separator between fields
    
    // Constructor for int-only fields
    MenuItem(const String& lbl, const Field& field, bool edit = true, const String& sep = " ")
        : label(lbl), fields{field}, editable(edit), fieldSeparator(sep) {}

    // Constructor for multiple fields
    MenuItem(const String& lbl, std::initializer_list<Field> fieldList, bool edit = true, const String& sep = " ")
        : label(lbl), fields(fieldList), editable(edit), fieldSeparator(sep) {}

    // Constructor for list-type field
    MenuItem(const String& lbl, const std::vector<String>& list, int defaultIndex = 0, bool edit = true, const String& sep = " ")
        : label(lbl), fields{ Field(list, 0) }, editable(edit), fieldSeparator(sep) {}

}; 
struct Menu {
public:
        //TODO:create IMENU class and inherit from it
    // Menu(String tittle) : header(tittle){};
    using RefreshCallback   = std::function<void(const String&, const String&, const String&, uint8_t)>;
    using HighlightCallback = std::function<void(uint8_t)>;
    using CloseCallback = std::function<void()>;
    bool updateFieldExternally(const String &label, size_t fieldIndex, int newValue);
    Menu(const String &title, RefreshCallback refresh, HighlightCallback highlight, CloseCallback close )
    : header(title), refreshMenu(refresh), highlightItem(highlight), closeMenu(close) {}

    MenuItem&  addItem(const MenuItem& item);
    void show(); // Display menu
    
    void handleKey(AffaCommon::AffaKey key, bool isHold); // everything passes through this
    void handleMessage(const CAN_FRAME& frame);
    bool isActive() const; 
    
    //const MenuViewBox& getCurrentViewBox() const { return currentViewBox; }
    std::function<void(Field&)> onChange;
    
    private:
    void updateViewBox();
    void HighlightCurrentSelection();
  
    //Affa3NavDisplay &display;  // reference to display
    String header;         // Menu header
    std::vector<MenuItem> items;
    int selectedIndex = 0;   // Which item is selected
    int selectedRow = 0; // Which row is selected in the current window
    int editingFieldIndex = 0; // Which subfield is being edited
    bool editing = false;
   // MenuViewBox currentViewBox;

    bool active = false;
    // Navigation
    void selectNext();
    void selectPrev();

    void enterEditMode();
    void exitEditMode();
    void nextFieldOrExit();
    void editFieldValue(int delta, bool isHold = false); // Change field value by delta (1 or 10) or hold
    void printMenuToSerial(String header, int item1, int item2, uint8_t scrollLockIndicator, uint8_t selectedRow) ;
    //void displayItem(const MenuItem& item, bool selected, bool editing);
    // Helpers to get display strings
    // String getItemString(MenuItem item) const;
    String getItemString(size_t index) const;

    // // Helper for clamping values
    // void clampValue(MenuItem &item);

    // Get scroll lock indicator flags for menu (0x0B means arrows visible)
    uint8_t getScrollIndicator();
    RefreshCallback refreshMenu;
    HighlightCallback highlightItem;
    CloseCallback closeMenu;
     
};
