import fs from 'node:fs';
import path from 'node:path';

const directory = path.resolve('binaries/restir_tests');
fs.mkdirSync(directory, {recursive: true});
const scriptPath = path.join(directory, 'closed_room.lua');
fs.writeFileSync(scriptPath, `local fixture = {}
function fixture:Initialize(host)
    local boxes = {
        {0,-0.5,0,12,1,12}, {0,4.5,0,12,1,12},
        {-5.5,2,0,1,4,12}, {5.5,2,0,1,4,12},
        {0,2,-5.5,12,4,1}, {0,2,5.5,12,4,1}
    }
    for i,b in ipairs(boxes) do
        local e = World.CreateEntity()
        e:SetName('closed_room_'..i)
        e:SetTransient(true)
        e:SetParent(host)
        e:SetPosition(Vector3(b[1],b[2],b[3]))
        e:SetScale(Vector3(b[4],b[5],b[6]))
        local render = e:AddComponent(ComponentType.Render)
        render:SetMesh(MeshType.Cube)
        render:SetMaterial('project/liminal_space_resources/wallpaper.xml')
    end
end
return fixture
`);
const xmlPath = scriptPath.replaceAll('\\', '/').replaceAll('&', '&amp;').replaceAll('"', '&quot;');
const worldPath = path.join(directory, 'closed_room.world');
fs.writeFileSync(worldPath, `<?xml version="1.0"?>
<World name="closed_room">
 <ConsoleVariables>
  <Variable name="r.ray_traced_reflections" value="1" />
  <Variable name="r.ray_traced_shadows" value="1" />
  <Variable name="r.restir_pt" value="1" />
  <Variable name="r.fog" value="1.4" />
  <Variable name="r.vhs" value="0" />
  <Variable name="r.film_grain" value="0" />
 </ConsoleVariables>
 <Entities>
  <Entity name="camera" id="96799266697307222" active="true" position="0 2 0" rotation="0 0 0 1" scale="1 1 1">
   <camera exposure_mode="1" aperture="2.8" shutter_speed="0.0166667" iso="800" fov_horizontal="1.57079637" near_plane="0.1" far_plane="10000" projection="0" flags="0" />
  </Entity>
  <Entity name="closed_room" id="7734000000000000001" active="true" position="0 0 0" rotation="0 0 0 1" scale="1 1 1">
   <script file_path="${xmlPath}" />
  </Entity>
 </Entities>
</World>
`);
console.log(worldPath);
