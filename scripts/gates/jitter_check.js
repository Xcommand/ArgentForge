// Mirrors GfxCanvas::jitteredColour's colour maths and asks the one question that
// matters: with no roll at all, does the colour come back exactly as it went in.
// A soft brush passes over a pixel again, so anything but zero drift here would
// make repeated strokes change a pixel they shouldn't touch.
//
// Usage: node scripts/gates/jitter_check.js   - nothing to read, the maths is the
// whole input, which is why it's the only gate that runs without a build

function jitter(r8, g8, b8, dh, ds, dv)
{
	const r = r8 / 255.0, g = g8 / 255.0, b = b8 / 255.0;
	const high = Math.max(r, g, b), low = Math.min(r, g, b), delta = high - low;

	let hue = 0;
	if (delta > 0)
	{
		if (high === r)
			hue = (g - b) / delta + (g < b ? 6 : 0);
		else if (high === g)
			hue = (b - r) / delta + 2;
		else
			hue = (r - g) / delta + 4;
		hue /= 6;
	}
	let sat = high > 0 ? delta / high : 0;
	let val = high;

	hue += dh;
	sat *= 1 + ds;
	val *= 1 + dv;

	hue = hue - Math.floor(hue);
	sat = Math.min(1, Math.max(0, sat));
	val = Math.min(1, Math.max(0, val));

	const sixth = Math.floor(hue * 6) % 6;
	const part = hue * 6 - Math.floor(hue * 6);
	const p = val * (1 - sat);
	const q = val * (1 - part * sat);
	const t = val * (1 - (1 - part) * sat);
	const wheel = [
		[val, t, p],
		[q, val, p],
		[p, val, t],
		[p, q, val],
		[t, p, val],
		[val, p, q],
	][sixth];

	return wheel.map((c) => Math.floor(c * 255 + 0.5));
}

let worst = 0, worstAt = null, checked = 0;
for (let r = 0; r <= 255; r += 5)
	for (let g = 0; g <= 255; g += 5)
		for (let b = 0; b <= 255; b += 5)
		{
			const out = jitter(r, g, b, 0, 0, 0);
			checked++;
			for (let c = 0; c < 3; c++)
			{
				const want = [r, g, b][c], got = out[c];
				if (Math.abs(want - got) > worst)
				{
					worst = Math.abs(want - got);
					worstAt = { want, got, rgb: [r, g, b] };
				}
			}
		}

console.log(`round trip: ${checked} colours, worst channel off by ${worst}`, worstAt ? JSON.stringify(worstAt) : '');

// Then that a full swing of every setting still lands on a colour and not off into
// NaN or a negative byte
let bad = 0;
for (let i = 0; i < 20000; i++)
{
	const out = jitter(i % 256, (i * 7) % 256, (i * 13) % 256, (Math.random() * 2 - 1), (Math.random() * 2 - 1), (Math.random() * 2 - 1));
	if (out.some((c) => !Number.isFinite(c) || c < 0 || c > 255))
		bad++;
}
console.log(`full swing: ${bad} of 20000 rolls left 0..255`);
