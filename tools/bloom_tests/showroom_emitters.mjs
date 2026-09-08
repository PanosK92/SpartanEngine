// Recreate the showroom's three material assets after downloading project.7z,
// or after editing the corresponding light colors in ferrari_showcase.world.
// Assets belong in binaries/project and are intentionally excluded from Git.
import fs from 'node:fs';
const world=fs.readFileSync('worlds/ferrari_showcase.world','utf8');
const directory='binaries/project/ferrari_showcase_resources';
const template=fs.readFileSync(`${directory}/ceiling_light.xml`,'utf8');
for(const name of ['tube_front_warm','tube_right_red','tube_left_cyan']) {
    const entity=world.match(new RegExp(`<Entity name="${name}"[^>]*>([\\s\\S]*?)</Entity>`))?.[1];
    const light=entity?.match(/<light\b[^>]*\/>/)?.[0];
    if(!light)throw new Error(`Missing showroom light ${name}`);
    let material=template;
    const color=['r','g','b'].map(channel=>{
        const value=Number(light.match(new RegExp(`color_${channel}="([^"]+)"`))?.[1]);
        if(!Number.isFinite(value)||value<0)throw new Error(`Invalid ${name} color ${channel}`);
        material=material.replace(new RegExp(`<color_${channel}>[^<]*</color_${channel}>`),`<color_${channel}>${value}</color_${channel}>`);
        return value;
    });
    // Separate assets preserve each lamp's tint without changing the shared white
    // ceiling material or any other objects using it. Bloom receives this RGB.
    fs.writeFileSync(`${directory}/${name}_emitter.xml`,material);
    console.log(`${name}_emitter: linear RGB ${color.join(', ')}`);
}
