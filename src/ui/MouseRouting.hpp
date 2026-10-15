#pragma once
#include <array>

namespace hksr::framework::ui {
// Cursor visibility is independent of event ownership. A gesture belongs to
// the receiver of its first down, even after crossing a window boundary.
class MouseRouting {
    std::array<unsigned char,5> buttons_{};
public:
    bool Motion(bool inside) const {
        // Do not interrupt a game drag that entered the menu.
        for(auto owner:buttons_)if(owner==1)return false;
        for(auto owner:buttons_)if(owner==2)return true;
        return inside;
    }
    bool Button(unsigned button,bool down,bool inside) {
        auto& owner=buttons_.at(button);
        if(down) {if(!owner)owner=Motion(inside)?2:1;return owner==2;}
        const bool result=owner?owner==2:inside;owner=0;return result;
    }
    void Reset() {buttons_.fill(0);}
};
}
