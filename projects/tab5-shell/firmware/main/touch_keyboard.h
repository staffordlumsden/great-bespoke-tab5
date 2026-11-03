#pragma once
#include <string>
#include <string_view>
namespace tab5_shell {
class TouchKeyboard {
public:
    bool visible{false}, shift{false}, control{false}, symbols{false};
    static constexpr int top=414, row_height=54;
    static bool toggle_hit(int x,int y) { return x>=928&&x<1088&&y>=688&&y<716; }
    static constexpr std::string_view keys[4]={"1234567890","qwertyuiop","asdfghjkl;","zxcvbnm,./"};
    static constexpr std::string_view symbol_keys[4]={"!@#$%^&*()","-_=+[]{}\\|",";:'\"`~<>?/",",.01234567"};
    char label(int row,int col) const { return symbols?symbol_keys[row][col]:keys[row][col]; }
    static constexpr const char* actions[10]={"SHIFT","CTRL","SYM","SPACE","UP","DOWN","LEFT","RIGHT","BKSP","ENTER"};
    std::string tap(int x,int y) {
        if (!visible || x<0 || x>=1280 || y<top || y>=684) return {};
        int row=(y-top)/row_height;
        if (row==4) {
            int column=x/128;
            if (column==0) { shift=!shift; return {}; }
            if (column==1) { control=!control; return {}; }
            if (column==2) { symbols=!symbols; return {}; }
            if (column==3) return " ";
            if (column==4) return "\x1b[A";
            if (column==5) return "\x1b[B";
            if (column==6) return "\x1b[D";
            if (column==7) return "\x1b[C";
            if (column==8) return "\x7f";
            return "\r";
        }
        int column=x/128; char c=label(row,column);
        if (control && c>='a' && c<='z') { c=static_cast<char>(c-'a'+1); control=false; }
        else if (shift) {
            if (c>='a'&&c<='z') c-=32;
            else if (row==0) c=std::string_view("!@#$%^&*()")[column];
            else if (c==';') c=':'; else if (c==',') c='<'; else if (c=='.') c='>'; else if (c=='/') c='?';
            shift=false;
        }
        return std::string(1,c);
    }
};
} // namespace tab5_shell
