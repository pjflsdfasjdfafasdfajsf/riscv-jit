// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

// this is the state that is shared both between our host and guest
typedef struct {
    int x;
    int y;
} state;

// let's define an enum for all syscalls so we don't have any magic numbers
// in our code
enum syscalls {
    SYS_BEGIN_DRAWING,
    SYS_CLEAR_BACKGROUND,
    SYS_DRAW_RECTANGLE,
    SYS_END_DRAWING,
    SYS_IS_KEY_DOWN,
};

// the function that the host expects to be exported
#define UPDATE_FUNCTION_NAME "update"
#define UPDATE               void update(state *state)
