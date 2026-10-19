// Instrument states for Spitfire BBC Symphony Orchestra, read from the NKS presets shipped with the library.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const default_library_dir = path.join(os.homedir(), "Spitfire", "Spitfire Audio - BBC Symphony Orchestra");
const root_close = "</SPITFIREAUDIO_AUNTIE>";

function extract_xml(buffer)
{
    const text = buffer.toString("latin1");
    const start = text.indexOf("<?xml");
    const end = text.indexOf(root_close);
    if (start < 0 || end < 0)
    {
        return null;
    }
    return buffer.subarray(start, end + root_close.length).toString("utf8");
}

export function bbc_instruments(library_dir = process.env.BBC_LIBRARY_DIR ?? default_library_dir)
{
    const nks_dir = path.join(library_dir, "NKS");
    if (!fs.existsSync(nks_dir))
    {
        throw new Error(`BBC Symphony Orchestra NKS folder not found at ${nks_dir}`);
    }
    const instruments = [];
    for (const file of fs.readdirSync(nks_dir).filter((name) => name.endsWith(".nksf")).sort())
    {
        const xml = extract_xml(fs.readFileSync(path.join(nks_dir, file)));
        if (!xml)
        {
            continue;
        }
        const meta = xml.match(/<META[^>]*>/)?.[0] ?? "";
        const articulations = [];
        for (const match of xml.matchAll(/<ARTIC>([\s\S]*?)<\/ARTIC>/g))
        {
            const body = match[1];
            articulations.push({
                name: body.match(/id="a_name" value="([^"]*)"/)?.[1] ?? "",
                keyswitch: Number(body.match(/id="t_keyswitch" value="([^"]*)"/)?.[1] ?? -1),
            });
        }
        const key = file.replace(/^BBCSO_Discover_/, "").replace(/\.nksf$/, "").toLowerCase();
        instruments.push({
            key,
            name: meta.match(/ name="([^"]*)"/)?.[1] ?? key,
            family: meta.match(/ family="([^"]*)"/)?.[1] ?? "",
            articulations,
            xml,
        });
    }
    return instruments;
}

export function bbc_find_instrument(query)
{
    const instruments = bbc_instruments();
    const wanted = query.toLowerCase().replace(/[\s-]+/g, "_");
    const match = instruments.find((i) => i.key === wanted) ?? instruments.find((i) => i.key.includes(wanted) || i.name.toLowerCase().includes(query.toLowerCase()));
    if (!match)
    {
        throw new Error(`no BBC instrument matching '${query}'. Available: ${instruments.map((i) => i.key).join(", ")}`);
    }
    return match;
}
