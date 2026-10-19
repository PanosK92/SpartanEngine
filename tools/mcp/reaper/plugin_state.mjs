// Read and rewrite the XML embedded in JUCE plugin state ("VC2!" blocks) inside a REAPER track chunk.
// The rewrite keeps the byte length identical (padding with spaces) so every enclosing size field stays valid.

function find_fx_blocks(chunk)
{
    const blocks = [];
    const lines = chunk.split("\n");
    for (let i = 0; i < lines.length; i++)
    {
        if (/^\s*<(VST|CLAP)\b/.test(lines[i]))
        {
            let end = i + 1;
            while (end < lines.length && lines[end].trim() !== ">")
            {
                end++;
            }
            blocks.push({ header_line: i, first_data_line: i + 1, end_line: end });
            i = end;
        }
    }
    return { lines, blocks };
}

function decode_block(lines, block)
{
    const parts = [];
    for (let i = block.first_data_line; i < block.end_line; i++)
    {
        const text = lines[i].trim();
        parts.push({ line: i, indent: lines[i].match(/^\s*/)[0], bytes: Buffer.from(text, "base64") });
    }
    return { parts, data: Buffer.concat(parts.map((p) => p.bytes)) };
}

function locate_xml(data)
{
    const latin = data.toString("latin1");
    const magic = latin.indexOf("VC2!");
    if (magic < 0)
    {
        return null;
    }
    const length = data.readUInt32LE(magic + 4);
    const start = magic + 8;
    return { start, length, xml: data.subarray(start, start + length).toString("utf8").replace(/\0+$/, "") };
}

export function read_fx_xml(chunk, fx_index)
{
    const { lines, blocks } = find_fx_blocks(chunk);
    const block = blocks[fx_index];
    if (!block)
    {
        throw new Error(`no fx block ${fx_index} in track chunk (found ${blocks.length})`);
    }
    const { data } = decode_block(lines, block);
    const located = locate_xml(data);
    if (!located)
    {
        throw new Error("plugin state has no embedded JUCE XML (VC2!) block");
    }
    return { header: lines[block.header_line].trim(), xml: located.xml, capacity: located.length };
}

export function write_fx_xml(chunk, fx_index, new_xml)
{
    const { lines, blocks } = find_fx_blocks(chunk);
    const block = blocks[fx_index];
    if (!block)
    {
        throw new Error(`no fx block ${fx_index} in track chunk (found ${blocks.length})`);
    }
    const { parts, data } = decode_block(lines, block);
    const located = locate_xml(data);
    if (!located)
    {
        throw new Error("plugin state has no embedded JUCE XML (VC2!) block");
    }

    const original = data.subarray(located.start, located.start + located.length);
    const trailing_nulls = original.length - original.toString("latin1").replace(/\0+$/, "").length;
    let encoded = Buffer.from(new_xml.trim(), "utf8");
    const budget = located.length - trailing_nulls;
    if (encoded.length > budget)
    {
        throw new Error(`new XML is ${encoded.length} bytes but only ${budget} fit in the existing state; shorten it`);
    }
    const close = encoded.lastIndexOf("</");
    const padding = Buffer.alloc(budget - encoded.length, 0x20);
    encoded = close > 0 ? Buffer.concat([encoded.subarray(0, close), padding, encoded.subarray(close)]) : Buffer.concat([encoded, padding]);

    const updated = Buffer.from(data);
    encoded.copy(updated, located.start);

    let cursor = 0;
    for (const part of parts)
    {
        const slice = updated.subarray(cursor, cursor + part.bytes.length);
        lines[part.line] = part.indent + slice.toString("base64");
        cursor += part.bytes.length;
    }
    return lines.join("\n");
}
