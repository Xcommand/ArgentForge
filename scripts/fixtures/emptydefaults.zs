version "4.14"

// The same shapes through ZScript, where a flag line carries a ';'

class ZEmptyDefaults : Actor
{
	Default
	{
	}
}

class ZOneLineDefaults : Actor
{
	Default
	{
	}

	States
	{
		Spawn:
			TNT1 A -1;
			Stop;
	}
}

class ZGluedDefaults : Actor { default {} }
