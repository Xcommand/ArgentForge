#pragma once

namespace slade
{
// One state as the engine counts them: a frame letter of a state line. 'POSS ABCD 3' is four
// of these, not one, and a goto's number is a count of them, not of lines
struct StateSlot
{
	string label; // Written above the line; only its first frame carries it
	string sprite; // The four characters the line names, 'POSS'
	char   frame = 0; // Which letter of the line's frame string this is
	string line; // The state line's own text, comments gone: what an edit replaces as a unit
	size_t byte = 0; // Where the state's sprite name starts in the text it was read from
};

// A jump written in a states block, and the two states its number runs between
struct StateGoto
{
	string label;
	int    offset = 0;
	size_t number = string::npos; // The bytes of the number itself; npos when it's a plain 'goto Label'
	size_t digits = 0;
	int    from   = -1; // The state the jump was written after
	int    base   = -1; // The state its number counts from
	int    target = -1; // And the state it lands on; -1 when this actor has no such label
};

// One actor of a DECORATE or ZScript text, with the single state list the engine builds for it
// (states blocks of the same actor are joined in the order they appear)
struct StateActor
{
	string            name;
	vector<StateSlot> states;
	vector<StateGoto> gotos;
};

// Reads [text]'s states blocks: every actor, its states in engine order, and each jump
// resolved onto them. Text that isn't a states block is left out of the account entirely
vector<StateActor> readStateActors(string_view text);

// The number a jump needs after [count] states appear at [at] in its actor's list (negative
// count for states going away). A number moves only when the change lands strictly after the
// state it counts from and no further than the state it was heading for
int shiftedGoto(const StateGoto& jump, int at, int count);

// What one pass of the renumber did
struct GotoRepair
{
	size_t rewritten = 0; // numbers written over with their new value
	size_t skipped   = 0; // jumps left alone: their label or their target state is gone
};

// One number that needs writing, where it sits in [text] and what it should say
struct GotoRewrite
{
	size_t byte;
	size_t len;
	string value;
};

// The numbers that need writing, plus the jumps that couldn't be fixed because the edit took
// their label or their target away
struct GotoRewrites
{
	vector<GotoRewrite> edits;
	size_t              skipped = 0;
};

// The numbers in [text] that would have to change for every jump to land on the state its twin
// in [was] landed on. Handed out rather than applied, so a caller that is editing a live text
// buffer can make the insert and these writes one undo step
GotoRewrites gotoRewrites(string_view text, string_view was);

// Rewrites the goto numbers in [text] so every jump lands on the state it landed on in [was],
// the same text before the edit being compensated. Nothing else in [text] moves
GotoRepair renumberGotos(string& text, string_view was);
} // namespace slade
