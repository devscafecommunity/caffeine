#pragma once

/**
 * Caffeine UI — composable element kit (umbrella header).
 *
 * Include this in Doppio panels and plugins that want the Lego-style element
 * model. The lower-level free functions in Widgets.hpp remain available for
 * custom drawing and migration.
 *
 *   using namespace Caffeine::UIKit;
 *   UI::Panel panel("My Tool", "Short description");
 *   auto& card = panel.emplace<UI::Card>();
 *   card.emplace<UI::Label>("Hello");
 *   card.emplace<UI::Button>("Save", UI::ButtonVariant::Primary)
 *       .setOnClick([]{ doSomething(); });
 *   UIContext ctx;
 *   ctx.draw(panel);
 */

#include "caffeine/ui/Element.hpp"
#include "caffeine/ui/Container.hpp"
#include "caffeine/ui/Control.hpp"
#include "caffeine/ui/Display.hpp"
#include "caffeine/ui/Property.hpp"
#include "caffeine/ui/Canvas.hpp"
#include "caffeine/ui/Debug.hpp"
