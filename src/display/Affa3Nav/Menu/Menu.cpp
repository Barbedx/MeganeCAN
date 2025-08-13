#include "Menu.h"
#include <cstdio>
#include <algorithm> // for std::clamp

// ===== Add item =====
void Menu::addItem(const MenuItem &item)
{
    items.push_back(item);
}
void Menu::updateViewBox() {
    if (items.size() < 2) return; // minimal requirement

    int topIndex = (selectedRow == 0) ? selectedIndex : selectedIndex - 1;
    int bottomIndex = topIndex + 1;

    uint8_t scrollIndicator = 0x0C; // both arrows
    if (selectedIndex == 0) scrollIndicator = 0x0B; // bottom only
    else if (selectedIndex == (int)items.size() - 1) scrollIndicator = 0x07; // top only

    currentViewBox.header = header;
    currentViewBox.line1 = getItemString(items[topIndex]);
    currentViewBox.line2 = getItemString(items[bottomIndex]);
    currentViewBox.scrollIndicator = scrollIndicator;
}
// ===== Show menu =====
void Menu::show()
{
    if (items.empty())
    {
        Serial.println("Menu is empty!");
        return;
    }
    updateViewBox();



    // int topIndex = (selectedRow == 0) ? selectedIndex : selectedIndex - 1;
    // int bottomIndex = topIndex + 1;
    // MenuItem topItem = items[topIndex];
    // MenuItem bottomItem = items[bottomIndex];

    // showMenu(header, topItem, bottomItem, getScrollIndicator());

   // display.showMenu(header.c_str(), getItemString(topItem).c_str(), getItemString(bottomItem).c_str(), getScrollIndicator() );
   // HighlightSelection(selectedRow); // Highlight correct item dynamically
    

} 
void Menu::printMenuToSerial(String header, MenuItem item1, MenuItem item2, uint8_t scrollLockIndicator, uint8_t selectedRow)
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




String Menu::getItemString(const MenuItem &item) const
{

    String out = (editing ? "*" : "") + item.label;

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
        if (editing && editingFieldIndex == (int)i)
        {
            val = "<" + val + ">";
        }

        out += item.fieldSeparator + val;
    }

    return out;
}

uint8_t Menu::getScrollIndicator()
{
    uint8_t scrollLockIndicator = 0x0C; // both arrows
    // int selectedRow = 1;
    // int selectedIndex = 1;
    if ((selectedRow == 0 && selectedIndex == 0) || (selectedRow == 1 && selectedIndex == 1))
    {
        scrollLockIndicator = 0x0B; // bottom arrow only
    }

    if ((selectedRow == 0 && selectedIndex == items.size() - 2) || (selectedRow == 1 && selectedIndex == items.size() - 1))
    {
        scrollLockIndicator = scrollLockIndicator == 0x0B ? 0x00 : 0x07; // top arrow only
    }
    return scrollLockIndicator;
}

void Menu::handleKey(AffaCommon::AffaKey key, bool isHold)
{
    if (!active)
    {
        if (isHold && key == AffaCommon::AffaKey::Load)
        {
            active = true;
            show(); // show menu at current selectedIndex
        }
        return;
    }

    switch (key)
    {

    case AffaCommon::AffaKey::RollUp:
        if (editing)
        {
            editFieldValue(isHold ? 10 : 1);
        }
        else
        {
            selectPrev();
        }

        break;

    case AffaCommon::AffaKey::RollDown:
        if (editing)
        {
            editFieldValue(isHold ? -10 : -1);
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
                            // closeMenu(); TODO: Implement closeMenu Maybe with just set aux text?
        }
        else
        {
            if (editing)
                nextFieldOrExit(); // go to next field or exit edit mode

            else
                enterEditMode();
            // enter edit mode if item editable
        }
        break;
    default:
        break;
    }
}

void Menu::editFieldValue(int delta)
{
    MenuItem &current = items[selectedIndex];
    Field &field = current.fields[editingFieldIndex];

    if (field.type == FieldType::Integer)
    {
        field.intValue += delta * field.step;
        if (field.intValue < field.minValue)
            field.intValue = field.minValue;
        if (field.intValue > field.maxValue)
            field.intValue = field.maxValue;
    }
    else if (field.type == FieldType::List)
    {
        field.listIndex += delta;
        if (field.listIndex < 0)
            field.listIndex = 0;
        if (field.listIndex >= (int)field.list.size())
            field.listIndex = field.list.size() - 1;
    }

    if (field.onChange)
    {
        field.onChange(field);
    }
    show(); // refresh display
}

void Menu::HighlightCurrentSelection(int row)
{
   // display.highlightItem(row);
    printMenuToSerial(header, items[selectedIndex], items[selectedIndex + 1], getScrollIndicator(), row);
}

void Menu::HighlightSelection(int row)
{
   // display.highlightItem(row);
    printMenuToSerial(header, items[selectedIndex], items[selectedIndex + 1], getScrollIndicator(), row);
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
            HighlightSelection(selectedRow);
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
            HighlightSelection(selectedRow);
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
    editingFieldIndex = 0;
    editing = true;
    show(); // refresh display
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
