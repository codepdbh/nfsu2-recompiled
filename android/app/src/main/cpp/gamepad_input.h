#pragma once
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <array>
#include <cmath>
#include <cstdint>

// Raw pad state for the guest's DirectInput joystick (guest_input.inc): sticks
// and triggers in -1..1 / 0..1, buttons in the XInput-for-DirectInput order
// (A B X Y LB RB Back Start LS RS) and the d-pad as a hat.
struct GuestPadState {
    float leftX=0,leftY=0,rightX=0,rightY=0,leftTrigger=0,rightTrigger=0;
    uint32_t buttons=0;
    int hatX=0,hatY=0;
};

// Physical gamepads (Bluetooth or USB). The game reads them as a DirectInput
// joystick (GuestPadState). Until it acquires that joystick, and on screens that
// only take the keyboard, buttons, sticks, triggers and the hat are also folded
// into DirectInput scan codes, with a racing layout while driving and a menu
// layout elsewhere. Keys are recomputed from the whole pad state on every
// event, so a button and a stick that both mean "left" never release each other.
class GamepadInput {
public:
    enum Button : unsigned {A,B,X,Y,L1,R1,L2,R2,Start,Select,ThumbL,ThumbR,Up,Down,Left,Right,ButtonCount};
    template<class SetKey>
    bool key(const AInputEvent* event,bool driving,SetKey&& setKey){
        int32_t code=AKeyEvent_getKeyCode(event),action=AKeyEvent_getAction(event);
        int button=buttonFor(code);
        if(button<0){
            if(isGamepad(event)&&action==AKEY_EVENT_ACTION_DOWN&&AKeyEvent_getRepeatCount(event)==0)
                __android_log_print(ANDROID_LOG_INFO,"NFSU2","Gamepad key %d not mapped (device %d)",code,AInputEvent_getDeviceId(event));
            return false;
        }
        if(action!=AKEY_EVENT_ACTION_DOWN&&action!=AKEY_EVENT_ACTION_UP)return true;
        bool down=action==AKEY_EVENT_ACTION_DOWN;
        if(down&&!buttons_[button]){
            pressedDriving_[button]=driving;pressedScreen_[button]=screen_;
            if(console_&&!driving&&((screen_==MapScreen&&button==A)||(screen_==MailboxScreen&&button==X)))button1Request_=true;
        }
        buttons_[button]=down;
        apply(driving,setKey);return true;
    }
    template<class SetKey>
    bool motion(const AInputEvent* event,bool driving,SetKey&& setKey){
        if(!(AInputEvent_getSource(event)&AINPUT_SOURCE_CLASS_JOYSTICK))return false;
        auto axis=[&](int32_t which){return AMotionEvent_getAxisValue(event,which,0);};
        stickX_=axis(AMOTION_EVENT_AXIS_X);stickY_=axis(AMOTION_EVENT_AXIS_Y);
        float hatX=axis(AMOTION_EVENT_AXIS_HAT_X),hatY=axis(AMOTION_EVENT_AXIS_HAT_Y);
        // A D-pad reported as a hat: remember the mode each direction was pressed in.
        if(hatX<-.5f&&!(hatX_<-.5f))hatDriving_[0]=driving;
        if(hatX>.5f&&!(hatX_>.5f))hatDriving_[1]=driving;
        if(hatY<-.5f&&!(hatY_<-.5f))hatDriving_[2]=driving;
        if(hatY>.5f&&!(hatY_>.5f))hatDriving_[3]=driving;
        hatX_=hatX;hatY_=hatY;
        // Android's standard right stick is Z/RZ.
        rightX_=axis(AMOTION_EVENT_AXIS_Z);rightY_=axis(AMOTION_EVENT_AXIS_RZ);
        // Pads report triggers as LTRIGGER/RTRIGGER, BRAKE/GAS or both.
        leftTrigger_=std::fmax(axis(AMOTION_EVENT_AXIS_LTRIGGER),axis(AMOTION_EVENT_AXIS_BRAKE));
        rightTrigger_=std::fmax(axis(AMOTION_EVENT_AXIS_RTRIGGER),axis(AMOTION_EVENT_AXIS_GAS));
        apply(driving,setKey);return true;
    }
    // The game switched between menus and driving, or started/stopped reading
    // the pad as a joystick: remap whatever is held.
    template<class SetKey> void refresh(bool driving,SetKey&& setKey){if(driving!=driving_||emulate_!=sentEmulation_)apply(driving,setKey);}
    template<class SetKey> void releaseAll(SetKey&& setKey){
        buttons_.fill(false);stickX_=stickY_=rightX_=rightY_=hatX_=hatY_=leftTrigger_=rightTrigger_=0;apply(driving_,setKey);
    }
    // false while the game reads the pad as a DirectInput joystick.
    void setKeyEmulation(bool emulate){emulate_=emulate;}
    // Launcher "Controles del mando": the port's layout, or the console versions'
    // one, where every function has a button (map, SMS, career stats, engage).
    void setConsoleLayout(bool console){console_=console;}
    // Menu screen the game shows (console layout): the world map and an SMS
    // message give some buttons another meaning, as on the consoles.
    // MenuScreen also covers any confirmation dialog shown over the others.
    enum Screen : int {MenuScreen=0,MapScreen=1,SmsMessageScreen=2,MailboxScreen=3};
    void setScreen(int screen){screen_=screen;}
    // A button meaning PAD_BUTTON1 was pressed: Cross on the world map (set the
    // GPS) or Square in the SMS mailbox (delete the message). No PC key sends
    // that message, so the caller queues it to the screen itself.
    bool takeButton1Request(){bool request=button1Request_;button1Request_=false;return request;}
    GuestPadState padState()const{
        GuestPadState state;
        state.leftX=stickX_;state.leftY=stickY_;state.rightX=rightX_;state.rightY=rightY_;
        state.leftTrigger=std::fmax(leftTrigger_,buttons_[L2]?1.f:0.f);
        state.rightTrigger=std::fmax(rightTrigger_,buttons_[R2]?1.f:0.f);
        // Console layout: the game's joystick keeps only the sticks and triggers
        // (analog steering and throttle); every button reaches it as a key instead.
        if(console_)return state;
        static constexpr Button order[]={A,B,X,Y,L1,R1,Select,Start,ThumbL,ThumbR};
        for(unsigned i=0;i<10;++i)if(buttons_[order[i]])state.buttons|=1u<<i;
        state.hatX=buttons_[Left]||hatX_<-.5f?-1:buttons_[Right]||hatX_>.5f?1:0;
        state.hatY=buttons_[Up]||hatY_<-.5f?-1:buttons_[Down]||hatY_>.5f?1:0;
        return state;
    }
    static bool isGamepad(const AInputEvent* event){
        int32_t source=AInputEvent_getSource(event);
        return (source&AINPUT_SOURCE_GAMEPAD)==AINPUT_SOURCE_GAMEPAD||(source&AINPUT_SOURCE_JOYSTICK)==AINPUT_SOURCE_JOYSTICK;
    }
private:
    std::array<bool,ButtonCount> buttons_{};
    float stickX_=0,stickY_=0,rightX_=0,rightY_=0,hatX_=0,hatY_=0,leftTrigger_=0,rightTrigger_=0;
    std::array<bool,256> sent_{};
    bool driving_=false,emulate_=true,sentEmulation_=true,console_=false;
    // Whether each button (and hat direction: left, right, up, down) was pressed
    // while driving. A button held across a switch between driving and menus keeps
    // its first meaning until released: Start pauses (Esc), the pause menu opens
    // with Start still held, and that must not become the menu's Enter (which
    // closed the menu again at once).
    std::array<bool,ButtonCount> pressedDriving_{};
    std::array<bool,4> hatDriving_{};
    std::array<int,ButtonCount> pressedScreen_{};
    int screen_=MenuScreen;bool button1Request_=false;

    static int buttonFor(int32_t code){
        switch(code){
        case AKEYCODE_BUTTON_A:case AKEYCODE_BUTTON_1:return A;
        case AKEYCODE_BUTTON_B:case AKEYCODE_BUTTON_2:return B;
        case AKEYCODE_BUTTON_X:case AKEYCODE_BUTTON_3:return X;
        case AKEYCODE_BUTTON_Y:case AKEYCODE_BUTTON_4:return Y;
        case AKEYCODE_BUTTON_L1:case AKEYCODE_BUTTON_5:return L1;
        case AKEYCODE_BUTTON_R1:case AKEYCODE_BUTTON_6:return R1;
        case AKEYCODE_BUTTON_L2:case AKEYCODE_BUTTON_7:return L2;
        case AKEYCODE_BUTTON_R2:case AKEYCODE_BUTTON_8:return R2;
        case AKEYCODE_BUTTON_SELECT:case AKEYCODE_BUTTON_9:case AKEYCODE_BACK:return Select;
        case AKEYCODE_BUTTON_START:case AKEYCODE_BUTTON_10:case AKEYCODE_MENU:return Start;
        case AKEYCODE_BUTTON_THUMBL:case AKEYCODE_BUTTON_11:return ThumbL;
        case AKEYCODE_BUTTON_THUMBR:case AKEYCODE_BUTTON_12:return ThumbR;
        case AKEYCODE_DPAD_UP:return Up;case AKEYCODE_DPAD_DOWN:return Down;
        case AKEYCODE_DPAD_LEFT:return Left;case AKEYCODE_DPAD_RIGHT:return Right;
        case AKEYCODE_DPAD_CENTER:return A;
        default:return -1;
        }
    }
    template<class SetKey> void apply(bool driving,SetKey& setKey){
        std::array<bool,256> want{};
        // Hysteresis keeps a stick resting near the threshold from chattering.
        auto past=[&](float value,unsigned scan){return value>(sent_[scan]?.25f:.4f);};
        bool left=buttons_[Left]||hatX_<-.5f||past(-stickX_,0xcb),right=buttons_[Right]||hatX_>.5f||past(stickX_,0xcd);
        bool up=buttons_[Up]||hatY_<-.5f,down=buttons_[Down]||hatY_>.5f;
        if(console_){applyConsole(driving,want,left,right,up,down,past);commit(driving,want,setKey);return;}
        want[0xcb]=left;want[0xcd]=right;
        if(driving){
            // Xbox-style racing: RT gas, LT brake/reverse, A handbrake, B nitrous,
            // X camera, Y look back, RB/LB shift up/down, Start pause.
            want[0xc8]=up||buttons_[R2]||past(rightTrigger_,0xc8);
            want[0xd0]=down||buttons_[L2]||past(leftTrigger_,0xd0);
            want[0x39]=buttons_[A];want[0x38]=buttons_[B];want[0x2e]=buttons_[X];want[0x30]=buttons_[Y];
            want[0x2a]=buttons_[R1];want[0x1d]=buttons_[L1];
            // Start/Select keep the meaning of the mode they were pressed in (see pressedDriving_).
            want[0x01]=(buttons_[Start]&&pressedDriving_[Start])||(buttons_[Select]&&pressedDriving_[Select]);
            want[0x1c]=buttons_[Start]&&!pressedDriving_[Start];
        }else{
            want[0xc8]=up||past(-stickY_,0xc8);want[0xd0]=down||past(stickY_,0xd0);
            want[0x1c]=buttons_[A]||(buttons_[Start]&&!pressedDriving_[Start]);
            want[0x01]=buttons_[B]||(buttons_[Select]&&!pressedDriving_[Select])||(buttons_[Start]&&pressedDriving_[Start]);
            want[0x39]=buttons_[X];want[0x2a]=buttons_[Y];
        }
        if(!emulate_)want.fill(false);
        commit(driving,want,setKey);
    }
    template<class SetKey> void commit(bool driving,const std::array<bool,256>& want,SetKey& setKey){
        driving_=driving;sentEmulation_=emulate_;
        for(unsigned scan=0;scan<256;++scan)if(want[scan]!=sent_[scan]){sent_[scan]=want[scan];setKey(scan,want[scan]);}
    }
    // Console layout (the console manuals' scheme: D-pad for the career screens),
    // sent as the PC game's own keyboard actions (its DIACTION table at 0x7FCAC0).
    // Steering and pedals go through the joystick axes while the game reads the
    // pad; only when it does not are they sent as keys too.
    template<class Past> void applyConsole(bool driving,std::array<bool,256>& want,bool,bool,bool,bool,Past& past){
        // Held in a given mode: a button pressed while driving (or in menus).
        auto held=[&](Button b,bool inDriving){return buttons_[b]&&pressedDriving_[b]==inDriving;};
        auto dpad=[&](Button b,bool inDriving){
            if(held(b,inDriving))return true;
            switch(b){
            case Left:return hatX_<-.5f&&hatDriving_[0]==inDriving;
            case Right:return hatX_>.5f&&hatDriving_[1]==inDriving;
            case Up:return hatY_<-.5f&&hatDriving_[2]==inDriving;
            case Down:return hatY_>.5f&&hatDriving_[3]==inDriving;
            default:return false;
            }
        };
        bool sticksAsKeys=emulate_;
        // Buttons pressed while driving: the game's driving actions.
        want[0x39]|=held(A,true);                        // Cross: handbrake
        want[0x38]|=held(X,true);                        // Square: nitrous
        want[0x2a]|=held(Y,true);                        // Triangle: shift up
        want[0x1d]|=held(B,true);                        // Circle: shift down
        want[0x2e]|=held(R1,true)||held(ThumbR,true);    // R1 / R3: camera
        want[0x30]|=held(L1,true);                       // L1: look back
        want[0x01]|=held(Start,true);                    // Start: pause
        want[0x13]|=held(Select,true);                   // Select: reset the car
        want[0x1c]|=dpad(Up,true);                       // D-pad up: enter shop / challenge
        want[0x14]|=dpad(Down,true);                     // D-pad down: career stats (Legend)
        want[0x32]|=dpad(Left,true);                     // D-pad left: world map
        want[0x0f]|=dpad(Right,true);                    // D-pad right: SMS messages
        want[0xc8]|=held(R2,true);want[0xd0]|=held(L2,true);  // digital triggers
        // Buttons pressed in menus: the game's menu actions (Enter = PAD_ACCEPT,
        // Esc = PAD_BACK). On the world map and in an SMS message, as on consoles:
        //  - map (0x004E08E0): Circle closes it (back), Square hides/shows the
        //    selected item (accept on it, 0x004B0EF0), Cross sets the GPS
        //    (PAD_BUTTON1, sent through takeButton1Request);
        //  - SMS message (0x00497B00): Cross returns to the mailbox (back),
        //    Circle closes the message and returns to the game (accept);
        //  - SMS mailbox (0x004CCC10): Cross opens, Circle closes, Square asks to
        //    delete the message (PAD_BUTTON1, 0x004CCE5C).
        // Confirmation dialogs over them count as MenuScreen: Cross confirms,
        // Circle cancels, as in every other menu.
        auto heldOn=[&](Button b,int screen){return held(b,false)&&pressedScreen_[b]==screen;};
        bool usual=false;  // the button keeps its usual menu meaning
        for(int screen:{MenuScreen,MailboxScreen})usual=usual||heldOn(A,screen);
        want[0x1c]|=usual||heldOn(B,SmsMessageScreen)||heldOn(X,MapScreen)||held(Start,false);
        want[0x01]|=heldOn(B,MenuScreen)||heldOn(B,MapScreen)||heldOn(B,MailboxScreen)||heldOn(A,SmsMessageScreen)||held(Select,false);
        want[0x33]|=heldOn(X,MenuScreen)||heldOn(X,SmsMessageScreen);want[0x34]|=held(Y,false);  // Menu buttons 2 and 3
        want[0x1a]|=held(L1,false);want[0x1b]|=held(R1,false); // Menu L1 / R1
        want[0xc7]|=held(R2,false);want[0xcf]|=held(L2,false); // Map zoom in / out
        want[0xcb]|=dpad(Left,false);want[0xcd]|=dpad(Right,false);
        want[0xc8]|=dpad(Up,false);want[0xd0]|=dpad(Down,false);
        // Sticks and analog triggers follow the current mode. While the game reads
        // the joystick it already gets them as axes: keys only when it does not.
        if(driving){
            want[0x30]|=past(rightY_,0x30);              // right stick down: look back
            if(sticksAsKeys){
                want[0xcb]|=past(-stickX_,0xcb);want[0xcd]|=past(stickX_,0xcd);
                want[0xc8]|=past(rightTrigger_,0xc8);want[0xd0]|=past(leftTrigger_,0xd0);
            }
        }else{
            want[0xc7]|=past(rightTrigger_,0xc7);want[0xcf]|=past(leftTrigger_,0xcf);
            if(sticksAsKeys){
                want[0xcb]|=past(-stickX_,0xcb);want[0xcd]|=past(stickX_,0xcd);
                want[0xc8]|=past(-stickY_,0xc8);want[0xd0]|=past(stickY_,0xd0);
            }
        }
    }
};
