#include "crpp/ui/skin.hpp"

namespace crpp::ui {

const Skin& cloud_skin();
const Skin& luna_skin();

const Skin& skin_of(SkinId id) { return id == SkinId::Luna ? luna_skin() : cloud_skin(); }

}  // namespace crpp::ui
