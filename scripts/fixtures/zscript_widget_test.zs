// A mod's own menu widgets, written the way the mods in the corpus write them: the class is
// the word the menu line uses with OptionMenuItem / ListMenuItem / ImageScrollerPage in front
// of it, and its Init() is the argument list the engine reads.

class OptionMenuItemTLTPOption : OptionMenuItemOption
{
	String mTooltip;

	OptionMenuItemTLTPOption Init(String label, String tooltip, Name tipColor, Name command, Name values, CVar graycheck = null, int center = 0)
	{
		mTooltip = tooltip;
		Super.Init(label, command, values, graycheck, center);
		return self;
	}
}

class ListMenuItemTLTPTextItem : ListMenuItemStaticText
{
	override void Init(ListMenuDescriptor list, int x, int y, String text, Name tipColor = 'Red')
	{
		Super.Init(list, x, y, text);
	}
}

// Not a menu widget: no parser ever builds this name out of a line
class NashGoreHelper : Actor
{
	void Init(int nope) { }
}

/* A widget inside a comment is not declared:
class OptionMenuItemGhost : OptionMenuItemOption
{
	OptionMenuItemGhost Init(String label) { }
}
*/

class ImageScrollerPageVRText : ImageScrollerPage
{
	void Init(ImageScrollerDescriptor desc, String text, int scale) { }
}
