#include "../Catch2/catch.hpp"

#include "caffeine/ui/UI.hpp"

#include <string>

using namespace Caffeine::UIKit;
using namespace Caffeine::UIKit::UI;

TEST_CASE("UI element tree composes small elements into cards in panels",
          "[ui][elements]") {
    Panel panel("Settings", "Element composition");
    auto& card = panel.emplace<Card>("General");
    auto& row = card.emplace<Row>("Actions");
    auto& button = row.emplace<Button>("Save", ButtonVariant::Primary);
    button.setId("save");
    auto& label = row.emplace<Label>("Hello");

    REQUIRE(panel.find("save") == &button);
    REQUIRE(panel.find("Actions") == &row);
    REQUIRE(panel.find("General") == &card);
    REQUIRE(panel.find("nope") == nullptr);
    REQUIRE(panel.isContainer());
    REQUIRE(card.isContainer());
    REQUIRE(button.isContainer() == false);

    std::string description;
    panel.describe(description);
    REQUIRE(description.find("Panel #Settings") != std::string::npos);
    REQUIRE(description.find("Card #General") != std::string::npos);
    REQUIRE(description.find("Row #Actions") != std::string::npos);
    REQUIRE(description.find("Button #save") != std::string::npos);
    REQUIRE(description.find("Label") != std::string::npos);

    label.setVisible(false);
    description.clear();
    panel.describe(description);
    REQUIRE(description.find("(hidden)") != std::string::npos);
}

TEST_CASE("UI elements get stable sibling ids and can be found by id",
          "[ui][elements]") {
    Column column;
    std::string first;
    std::string second;
    auto& a = column.emplace<TextInput>(first);
    auto& b = column.emplace<TextInput>(second);
    REQUIRE(!a.id().empty());
    REQUIRE(!b.id().empty());
    REQUIRE(a.id() != b.id());
    REQUIRE(column.find(a.id()) == &a);
    REQUIRE(column.find(b.id()) == &b);

    auto& explicitButton = column.emplace<Button>("Explicit");
    explicitButton.setId("my_button");
    REQUIRE(column.find("my_button") == &explicitButton);

    a.setEnabled(false);
    std::string description;
    column.describe(description);
    REQUIRE(description.find("(disabled)") != std::string::npos);
}

TEST_CASE("UI property grids expose typed rows as elements", "[ui][elements]") {
    PropertyGrid grid("Properties");
    float value = 2.5f;
    bool enabled = true;
    int choice = 1;

    auto& floatRow = grid.typedRow<FloatProperty>("Value", value, 0.0f, 10.0f);
    auto& boolRow = grid.typedRow<BoolProperty>("Enabled", enabled);
    auto& choiceRow =
        grid.typedRow<ChoiceProperty>("Mode", choice,
                                      std::vector<std::string>{"One", "Two", "Three"});

    REQUIRE(grid.find("Property:Value") == &floatRow);
    REQUIRE(grid.find("Property:Enabled") == &boolRow);
    REQUIRE(grid.find("Property:Mode") == &choiceRow);
    REQUIRE(floatRow.value() != nullptr);
    REQUIRE(boolRow.value() != nullptr);
    REQUIRE(choiceRow.value() != nullptr);
    REQUIRE(floatRow.children().size() == 1);
    REQUIRE(boolRow.children().size() == 1);

    std::string description;
    grid.describe(description);
    REQUIRE(description.find("PropertyGrid") != std::string::npos);
    REQUIRE(description.find("FloatProperty") != std::string::npos);
    REQUIRE(description.find("BoolProperty") != std::string::npos);
    REQUIRE(description.find("ChoiceProperty") != std::string::npos);
}

TEST_CASE("UI row, column and card expose their children in order", "[ui][elements]") {
    Card card("Order");
    auto& first = card.emplace<Label>("first");
    auto& second = card.emplace<Label>("second");
    auto& third = card.emplace<Label>("third");

    const std::vector<Element*> children = card.children();
    REQUIRE(children.size() == 3);
    REQUIRE(children[0] == &first);
    REQUIRE(children[1] == &second);
    REQUIRE(children[2] == &third);
}
