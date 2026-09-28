// Regenerates dist/res/config/actor_flags.cfg from the 'Actor flags (used in an
// actor's definition)' constants of config/languages/zscript.txt. The flag names
// themselves always come from that file, so the vocabulary stays a copy of data
// the project already maintains; the generator only adds a curated category per
// flag (what the flag is for), which zscript.txt doesn't have. Every flag must
// be assigned to exactly one category or the script complains.
//
// Usage: node scripts/gen/gen_actor_flags.js
const fs   = require('fs');
const path = require('path');

// Whatever the script is called from, the two files are these
const repo = path.join(__dirname, '..', '..');
const src  = path.join(repo, 'dist/res/config/languages/zscript.txt');
const out  = path.join(repo, 'dist/res/config/actor_flags.cfg');

const lines = fs.readFileSync(src, 'utf8').split(/\r?\n/);
const start = lines.findIndex((l) => l.includes("Actor flags (used in an actor's definition)"));
const end   = lines.findIndex((l, i) => i > start && /^\s*}\s*$/.test(l));

if (start < 0 || end < 0)
	throw new Error(`could not find the actor flags section (${start}, ${end})`);

const known = new Set();

for (let i = start + 1; i < end; ++i)
{
	const line = lines[i].trim();
	if (!line || line.startsWith('//'))
		continue;

	for (let word of line.split(','))
	{
		word = word.trim();
		if (word)
			known.add(word);
	}
}

const categories = [
	[
		'Movement',
		'DROPOFF FLOAT INFLOAT NOCLIP NOGRAVITY NOLIFTDROP CANNOTPUSH CANPASS CANPUSHWALLS ' +
		'CANTLEAVEFLOORPIC FLOATBOB FLOORCLIP PUSHABLE SLIDESONWALLS SPAWNFLOAT THRUACTORS ' +
		'THRUGHOST WINDTHRUST NODAMAGETHRUST CEILINGHUGGER FLOORHUGGER DONTOVERLAP DONTSQUASH ' +
		'NOBLOCKMONST CANUSEWALLS DONTFALL NOFORWARDFALL NODROPOFF MOVEWITHSECTOR GHOST CANJUMP ' +
		'JUMPDOWN MTHRUSPECIES THRUSPECIES DONTTHRUST STAYONLIFT FALLDAMAGE NOFRICTION ' +
		'NOFRICTIONBOUNCE MVISBLOCKED RELATIVETOFLOOR CROSSLINECHECK SPECIALFLOORCLIP',
	],
	[
		'Combat',
		'AMBUSH FRIENDLY JUSTATTACKED JUSTHIT SKULLFLY BLASTED DONTSEEKINVISIBLE AVOIDMELEE ' +
		'ISMONSTER NOTARGET SCREENSEEKER CANTSEEK QUICKTORETALIATE SEESDAGGERS INCOMBAT ' +
		'LOOKALLAROUND STANDSTILL FRIGHTENED ALWAYSFAST NEVERFAST NOINFIGHTING FORCEINFIGHTING ' +
		'NOINFIGHTSPECIES NOPAIN USESPECIAL NOFEAR TOUCHY SEEINVISIBLE NOTAUTOAIMED ' +
		'NEVERTARGET DONTFOLLOWPLAYERS MASTERNOSEE SEEFRIENDLYMONSTERS MINVISIBLE AVOIDHAZARDS ' +
		'TELEPORT BOSS NOVERTICALMELEERANGE NOSPLASHALERT NOTARGETSWITCH',
	],
	[
		'Damage',
		'NOBLOOD NONSHOOTABLE INVULNERABLE RIPPER REFLECTIVE BLOODLESSIMPACT DONTGIB DONTSPLASH ' +
		'NORADIUSDMG FOILINVUL FORCERADIUSDMG OLDRADIUSDMG FORCEZERORADIUSDMG DONTHARMCLASS ' +
		'DEFLECT SHIELDREFLECT NOSHIELDREFLECT SPECTRAL STRIFEDAMAGE EXTREMEDEATH NOEXTREMEDEATH ' +
		'NOICEDEATH ICESHATTER DONTDRAIN DONTRIP NODAMAGE PAINLESS PIERCEARMOR ' +
		'ADDITIVEPOISONDAMAGE ADDITIVEPOISONDURATION DOHARMSPECIES DONTHARMSPECIES FORCEPAIN ' +
		'POISONALWAYS NOBOSSRIP ALLOWPAIN CAUSEPAIN HARMFRIENDS BUDDHA FOILBUDDHA NOTELEFRAG ' +
		'LAXTELEFRAGDMG ALWAYSTELEFRAG TELESTOMP NOTELESTOMP SMASHABLE DOSHADOWBLOCK SHADOWBLOCK ' +
		'FORCESECTORDAMAGE NOSECTORDAMAGE VULNERABLE THRUREFLECT MIRRORREFLECT NOTIMEFREEZE',
	],
	[
		'Projectiles',
		'MISSILE SEEKERMISSILE ALWAYSPUFF CANBLAST DONTBLAST EXPLOCOUNT SKYEXPLODE ' +
		'NOEXPLODEFLOOR PUFFONACTORS DEHEXPLOSION ACTIVATEIMPACT ACTIVATEMCROSS ACTIVATEPCROSS ' +
		'DONTREFLECT STEPMISSILE BLOCKEDBYSOLIDACTORS PUFFGETSOWNER ONLYSLAMSOLID ' +
		'RETARGETAFTERSLAM STOPRAILS ALLOWTHRUBITS AIMREFLECT SHADOWAIM SHADOWAIMVERT',
	],
	[
		'Visual',
		'SHADOW STEALTH ICECORPSE BRIGHT DONTTRANSLATE NOSKIN SPRITEANGLE FORCEDECAL NODECAL ' +
		'ALLOWPARTICLES BLOODSPLATTER NOBLOODDECALS ADDLIGHTLEVEL FRIGHTENING DONTFACETALKER ' +
		'FULLVOLACTIVE FULLVOLDEATH FULLVOLSEE DECOUPLEDANIMATIONS DONTCORPSE NOTONAUTOMAP ' +
		'VISIBILITYPULSE ROCKETTRAIL ' +
		'GRENADETRAIL INVISIBLE FORCEYBILLBOARD FORCEXYBILLBOARD ROLLSPRITE FLATSPRITE ' +
		'WALLSPRITE DONTFLIP ROLLCENTER MASKROTATION ABSMASKANGLE ABSMASKPITCH XFLIP YFLIP ' +
		'INTERPOLATEANGLES DONTINTERPOLATE SPRITEFLIP ZDOOMTRANS CASTSPRITESHADOW ' +
		'NOSPRITESHADOW INVISIBLEINMIRRORS ONLYVISIBLEINMIRRORS BILLBOARDFACECAMERA ' +
		'BILLBOARDNOFACECAMERA FLIPSPRITEOFFSETX FLIPSPRITEOFFSETY ISOMETRICSPRITES ' +
		'SQUAREPIXELS STRETCHPIXELS',
	],
	['Inventory', 'PICKUP DROPPED WEAPONSPAWN NOMENU'],
	['Players', 'NOTDMATCH BLOCKASPLAYER'],
	[
		'Level',
		'COUNTITEM COUNTKILL COUNTSECRET FIXMAPTHINGPOS SPAWNCEILING ALWAYSRESPAWN NEVERRESPAWN ' +
		'RANDOMIZE SPAWNSOUNDSOURCE NOTRIGGER E1M8BOSS E2M8BOSS E3M8BOSS E4M6BOSS E4M8BOSS ' +
		'MAP07BOSS1 MAP07BOSS2',
	],
	[
		'General',
		'SHOOTABLE SOLID NOSECTOR NOBLOCKMAP CORPSE DORMANT SPECIAL NOSAVEGAME NOINTERACTION',
	],
	[
		'Scripting',
		'ACTLIKEBRIDGE BOSSDEATH GETOWNER SYNCHRONIZED DONTMORPH STAYMORPHED NOTELEPORT ' +
		'NOTELEOTHER ALLOWTHRUFLAGS USEKILLSCRIPTS NOKILLSCRIPTS HITMASTER HITTARGET HITTRACER ' +
		'HITOWNER ABSVIEWANGLES ISPUFF BUMPSPECIAL SPECIALFIREDAMAGE CAMFOLLOWSPLAYER ' +
		'SUMMONEDMONSTER',
	],
	[
		'Bounce',
		'BOUNCEONWALLS BOUNCEONFLOORS BOUNCEONCEILINGS ALLOWBOUNCEONACTORS BOUNCEAUTOOFF ' +
		'BOUNCELIKEHERETIC CANBOUNCEWATER NOWALLBOUNCESND NOBOUNCESOUND BOUNCEONACTORS ' +
		'EXPLODEONWATER MBFBOUNCER BOUNCEAUTOOFFFLOORONLY USEBOUNCESTATE DONTBOUNCEONSHOOTABLES ' +
		'BOUNCEONUNRIPPABLES DONTBOUNCEONSKY',
	],
	['Deprecated', 'MISSILEMORE MISSILEEVENMORE'],
];

const assigned = new Map();
const problems = [];

for (const [category, text] of categories)
{
	for (const flag of text.trim().split(/\s+/))
	{
		if (!known.has(flag))
			problems.push(`${flag} (${category}) isn't a flag in zscript.txt`);
		if (assigned.has(flag))
			problems.push(`${flag} is in both '${assigned.get(flag)}' and '${category}'`);
		assigned.set(flag, category);
	}
}

for (const flag of known)
	if (!assigned.has(flag))
		problems.push(`${flag} isn't in any category`);

if (problems.length)
{
	console.error('Problems:');
	for (const p of problems)
		console.error('  ' + p);
	process.exit(1);
}

const body = categories.map(([category, text]) =>
{
	const flags = text.trim().split(/\s+/).filter((f) => known.has(f)).sort();
	return `\t${category} = { ${flags.join(', ')} }`;
});

// The hand-written part of the file: one note per flag. This step owns the flag
// names and their categories, and nothing else, so whatever sits under
// 'descriptions' is carried across as it stands
let docs = '';
if (fs.existsSync(out))
{
	const old = fs.readFileSync(out, 'utf8').replace(/\r\n/g, '\n');
	const at  = old.indexOf('\ndescriptions\n{');
	if (at >= 0)
		docs = old.slice(at + 1).trimEnd() + '\n';
}

const text = `// Actor flag vocabulary for the Actor Constructor
//
// The flag names come from the 'Actor flags (used in an actor's definition)'
// constants of config/languages/zscript.txt. The groups are categories of what
// the flags are for, curated in scripts/gen/gen_actor_flags.js, which regenerates this
// file and checks every real flag is in exactly one of them. Flags a file uses
// that aren't listed here still show up in the constructor.
flags
{
${body.join('\n')}
}

// One short line per flag, shown when the mouse is over it in the
// constructor. A flag that isn't listed here simply has no note.
${docs}`;

fs.writeFileSync(out, text.replace(/\n/g, '\r\n'), 'utf8');
const notes = (docs.match(/^\t[A-Z][A-Z0-9_]* = "/gm) || []).length;
console.log(`${categories.length} categories, ${assigned.size} flags, ${known.size} known, ${notes} notes kept`);
