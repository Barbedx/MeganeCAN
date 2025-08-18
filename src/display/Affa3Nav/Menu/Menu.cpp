#include "Menu.h"
#include <cstdio>
#include <algorithm> // for std::clamp

bool Menu::updateFieldExternally(const String &label, size_t fieldIndex, int newValue)
{

    for (size_t idx = 0; idx < items.size(); ++idx) {
        MenuItem &item = items[idx];
        if (item.label != label) continue;
        if (fieldIndex >= item.fields.size()) return false;

        Field &f = item.fields[fieldIndex];
        //int clamped = std::clamp(newValue, f.minValue, f.maxValue);
         // if (f.intValue == clamped) return true;

        f.intValue = newValue;

        // call field-level listener if assigned
        if (f.onChange) f.onChange(f);

        // call item-level callback if assigned
        if (item.onChange) item.onChange(item);

        // Only refresh display if menu is active **and item is visible**
        if (active) {
            // Determine the top and bottom item indices in the sliding window
            int topIndex = (selectedRow == 0) ? selectedIndex : selectedIndex - 1;
            int bottomIndex = topIndex + 1;

            if (idx == topIndex || idx == bottomIndex) {
                show(); // refresh display
            }
        }
        return true;
    }
    return false;
}

// ===== Add item =====
MenuItem&  Menu::addItem(const MenuItem &item)
{
    items.push_back(item);
    return items.back();
}
 
// ===== Show menu =====
void Menu::show()
{
    if (items.empty())
    {
        Serial.println("Menu is empty!");
        return;
    }
    

    int topIndex = (selectedRow == 0) ? selectedIndex : selectedIndex - 1;
    int bottomIndex = topIndex + 1;

     

    refreshMenu(header, getItemString(topIndex), getItemString(bottomIndex), getScrollIndicator());
    HighlightCurrentSelection(); // Highlight correct item dynamically

    // Update view box


    // int topIndex = (selectedRow == 0) ? selectedIndex : selectedIndex - 1;
    // int bottomIndex = topIndex + 1;
    // MenuItem topItem = items[topIndex];
    // MenuItem bottomItem = items[bottomIndex];

    // showMenu(header, topItem, bottomItem, getScrollIndicator());

   // display.showMenu(header.c_str(), getItemString(topItem).c_str(), getItemString(bottomItem).c_str(), getScrollIndicator() );
   // HighlightSelection(selectedRow); // Highlight correct item dynamically 

} 
void Menu::printMenuToSerial(String header, int item1, int item2, uint8_t scrollLockIndicator, uint8_t selectedRow)
{ 
  const auto &menu = items;
  const int total = menu.size();

  Serial.println("=== Menu ===");

  // Scroll indicators
  if (scrollLockIndicator == 0x07 || scrollLockIndicator == 0x0C)
    Serial.println("⬆️");

 
    if (selectedRow == 0)
      Serial.print("* ");
    else
      Serial.print("  ");
    Serial.println(getItemString(item1));
 

    if (selectedRow == 1)
      Serial.print("* ");
    else
      Serial.print("  ");
    Serial.println(getItemString(item2));
 
    
    

if (scrollLockIndicator == 0x0B || scrollLockIndicator == 0x0C)
    Serial.println("⬇️");

  Serial.println("----------------------");
}




String Menu::getItemString(size_t index) const
{ 
    const MenuItem &item = items[index]; 
    String out = (editing && index == selectedIndex ? "*" : "") + item.label;

    if (!item.fields.empty())
    {
        out += ":";
    }

    // Iterate over all fields of the item
    for (size_t i = 0; i < item.fields.size(); i++)
    {
        const Field &f = item.fields[i];
        String val;

        switch (f.type)
        {
        case FieldType::Integer:
            val = String(f.intValue);
            if (f.unit.length() > 0)
                val += f.unit; // e.g., "%"
            break;

            // case FieldType::Time:
            //     val = (f.hour < 10 ? "0" : "") + String(f.hour) + ":" +
            //           (f.minute < 10 ? "0" : "") + String(f.minute);
            //     break;

        case FieldType::List:
            if (f.listIndex >= 0 && f.listIndex < (int)f.list.size())
                val = f.list[f.listIndex];
            break;

        default:
            val = "?";
            break;
        }

        // Show edit mode formatting if needed
        if (editing && index == selectedIndex && editingFieldIndex == (int)i)
        {
            val = "<" + val + ">";
        }

        out += item.fieldSeparator + val;
    }

    return out;
}

uint8_t Menu::getScrollIndicator()
{

    uint8_t scrollIndicator = 0x0C; // both arrows
    if (selectedIndex == 0 ||
        (selectedIndex == 1 && selectedRow == 1))
     scrollIndicator = 0x0B; // bottom only
    else if (selectedIndex == (int)items.size() - 1 
        || (selectedIndex == (int)items.size() - 2 && selectedRow == 0 )
        ) scrollIndicator = 0x07; // top only
    return scrollIndicator;
}

void Menu::handleKey(AffaCommon::AffaKey key, bool isHold)
{
    if (!active)
    { 
        if (isHold && key == AffaCommon::AffaKey::Load)
        {
          active = true;
            show(); // refresh display
        }
        return ; // not active, do nothing //maybew add another menus later?(by catching new keys)
    }

    switch (key)
    {

    case AffaCommon::AffaKey::RollUp:
        if (editing)
        {
            editFieldValue(-1,isHold );
        }
        else
        {
            selectPrev();
        }

        break;

    case AffaCommon::AffaKey::RollDown:
        if (editing)
        {
            editFieldValue(1,isHold);
        }
        else
        {
            selectNext();
        }

        break;

    case AffaCommon::AffaKey::Load:
        if (isHold)
        {
            active = false; // exit menu
            closeMenu(); // call close callback
            Serial.println("Menu closed");
                            // closeMenu(); TODO: Implement closeMenu Maybe with just set aux text?
        }
        else
        {
            if (editing){
                nextFieldOrExit(); // go to next field or exit edit mode
                return ; // refresh display
            }
            else  
                enterEditMode(); 
        }
        break;
    default:
        break;
    }
    return ;
}

void  Menu::editFieldValue(int delta, bool isHold)
{
    MenuItem &current = items[selectedIndex];
    Field &field = current.fields[editingFieldIndex];

    if (field.type == FieldType::Integer)
    {

        delta *= isHold ? field.stepMultiplier : 1; // 1 or 10

        int newValue = field.intValue + delta * field.step;
        if (newValue < field.minValue) newValue = field.minValue;
        if( newValue > field.maxValue) newValue = field.maxValue;
         
        if(field.intValue == newValue) return; // no change

        field.intValue = newValue;
    }
    else if (field.type == FieldType::List)
    { 
        int newIndex = field.listIndex + delta;
        Serial.println("List index: " + String(newIndex) + " of " + String(field.list.size()));
        if (newIndex < 0) newIndex = 0;
        if (newIndex >= (int)field.list.size()-1) newIndex = field.list.size()-1; // no wrap around
        
        if (newIndex == field.listIndex) return; // no change  
        field.listIndex = newIndex;
        Serial.println("New list index: " + String(field.listIndex)); 
    }

    if (field.onChange) //refresh like that????!!!!!!
    {
        field.onChange(field);
    }
        // Item-level callback
    if (items[selectedIndex].onChange) {
        Serial.println("Cal,ling callback Item changed: " + items[selectedIndex].label);
        items[selectedIndex].onChange(items[selectedIndex]);
    }

    show(); // refresh display
}

void Menu::HighlightCurrentSelection()
{
   // display.highlightItem(row);
   // currentViewBox.selectedRow = row; // Update selected row in view box
//just hack to show in log as well 
    int topIndex = (selectedRow == 0) ? selectedIndex : selectedIndex - 1;
    int bottomIndex = topIndex + 1; 
    printMenuToSerial(header, topIndex, bottomIndex, getScrollIndicator(), selectedRow);
    if (highlightItem)
    {
        highlightItem(selectedRow); // Call the highlight callback
    }
}
 

bool Menu::isActive() const
{
    return active;
}

void Menu::selectNext()
{
    if (selectedIndex < items.size() - 1)
    { // not looping
        selectedIndex++;
        if (selectedRow == 0)
        {
            selectedRow = 1;
            HighlightCurrentSelection();
        }
        else
        {
            show(); // refresh display
        }
    }
}

void Menu::selectPrev()
{
    if (selectedIndex > 0)
    { // not looping
        selectedIndex--;
        if (selectedRow == 1)
        {
            selectedRow = 0;
            HighlightCurrentSelection();
        }
        else
        {
            show(); // refresh display
        }
    }
}

// ===== Edit mode control =====
void Menu::enterEditMode()
{
    if(items[selectedIndex].editable){

        editingFieldIndex = 0;
        editing = true;
        show();
    }
}

void Menu::exitEditMode()
{
    editing = false;
    show(); // refresh display
}

void Menu::nextFieldOrExit()
{
    MenuItem &current = items[selectedIndex];
    if (editingFieldIndex + 1 < (int)current.fields.size())
    {
        editingFieldIndex++;
        show(); // refresh display
    }
    else
    {
        exitEditMode();
    } 
}
