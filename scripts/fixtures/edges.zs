// ZScript shapes the constructor has to put back the way it read them

CLASS EdgeCases : Actor
{
	// a comment before the block

	default
	{

		Health = 30;
		Speed = 13.5;      // a trailing note
		+NOGRAVITY
		-MFRAGWHENKILLED
		+INVENTORY.ALWAYSPICKUP
		-Inventory.NeverRespawn
		Scale = 0.5;

	}

	var
	{
		int odd;
	}

	// spacing before the class brace
}

CLASS OneLiner : Actor
{
	default { Health = 5; }
}
