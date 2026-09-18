// Repair the Ferrari showcase assets supplied by older project.7z packages.
// Keep this asset utility outside test directories: the world depends on it.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const world = fs.readFileSync(path.join(root, 'worlds/ferrari_showcase.world'), 'utf8');
const directory = path.join(root, 'binaries/project/ferrari_showcase_resources');
const template = fs.readFileSync(path.join(directory, 'ceiling_light.xml'), 'utf8');
const materials = [];

for (const name of ['tube_front_warm', 'tube_right_red', 'tube_left_cyan']) {
    const entity = world.match(new RegExp(`<Entity name="${name}"[^>]*>([\\s\\S]*?)</Entity>`))?.[1];
    const light = entity?.match(/<light\b[^>]*\/>/)?.[0];
    if (!light) throw new Error(`Missing showroom light ${name}`);

    let material = template;
    for (const channel of ['r', 'g', 'b']) {
        const value = Number(light.match(new RegExp(`color_${channel}="([^"]+)"`))?.[1]);
        if (!Number.isFinite(value) || value < 0) throw new Error(`Invalid ${name} color ${channel}`);
        const property = new RegExp(`<color_${channel}>[^<]*</color_${channel}>`);
        if (!property.test(material)) throw new Error(`Missing template color ${channel}`);
        material = material.replace(property, `<color_${channel}>${value}</color_${channel}>`);
    }
    // The emitter must stay luminous even if the shared template was edited.
    const emission = /<emissive_from_albedo>[^<]*<\/emissive_from_albedo>/;
    if (!emission.test(material)) throw new Error('Missing template emission');
    material = material.replace(emission, '<emissive_from_albedo>1</emissive_from_albedo>');
    materials.push([path.join(directory, `${name}_emitter.xml`), material]);
}

for (const [destination, material] of materials) {
    // Avoid rewriting assets (and invalidating caches) when already repaired.
    if (!fs.existsSync(destination) || fs.readFileSync(destination, 'utf8') !== material) {
        fs.writeFileSync(destination, material);
    }
    console.log(`Ready: ${path.basename(destination)}`);
}
