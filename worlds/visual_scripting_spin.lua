-- A visual script. Spartan generated this file from the graph below, edit the graph in the editor's Visual Script window,
-- changes made to this file by hand are replaced the next time the graph is saved.
--[==[spartan-visual-script
<visual_script version="1" next_id="7">
  <view x="131.5" y="289.5" zoom="1" />
  <variables>
    <variable name="speed" type="number" value="90" />
  </variables>
  <nodes>
    <node id="1" type="util.comment" x="-20" y="-150">
      <data key="h" value="110" />
      <data key="text" value="LESSON 1, ON TICK&#10;On Tick fires once every frame. delta is the time since the last frame in seconds, so speed x delta turns the cube by speed degrees per second, no matter the frame rate. Change speed in the Variables panel or in the entity's Properties." />
      <data key="w" value="820" />
    </node>
    <node id="2" type="event.Tick" x="0" y="0" />
    <node id="3" type="api:Entity.Rotate" x="620" y="0" />
    <node id="4" type="api:Quaternion.FromEulerAngles" x="400" y="120" />
    <node id="5" type="math.multiply" x="200" y="140" />
    <node id="6" type="var.get:speed" x="20" y="170" />
  </nodes>
  <links>
    <link from="2" from_pin="then" to="3" to_pin="exec" />
    <link from="4" from_pin="rotation" to="3" to_pin="delta" />
    <link from="5" from_pin="result" to="4" to_pin="yaw" />
    <link from="6" from_pin="speed" to="5" to_pin="a" />
    <link from="2" from_pin="delta" to="5" to_pin="b" />
  </links>
</visual_script>
]==]

local script = {}
script.speed = 90

-- per entity state the graph keeps between frames, out of the inspector's sight
local vs = {}

function script:Tick(entity)
    vs.delta = Timer.GetDeltaTimeSec()
    -- Rotate [3]
    entity:Rotate(Quaternion.FromEulerAngles(0, (self.speed * vs.delta), 0))
end

return script
