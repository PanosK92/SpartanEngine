-- A visual script. Spartan generated this file from the graph below, edit the graph in the editor's Visual Script window,
-- changes made to this file by hand are replaced the next time the graph is saved.
--[==[spartan-visual-script
<visual_script version="1" next_id="14">
  <view x="120.693848" y="240" zoom="0.88481015" />
  <variables>
    <variable name="speed" type="number" value="3" />
    <variable name="bright" type="number" value="12000" />
    <variable name="dim" type="number" value="300" />
  </variables>
  <nodes>
    <node id="1" type="util.comment" x="-20" y="-160">
      <data key="h" value="110" />
      <data key="text" value="LESSON 4, BRANCH&#10;Branch is an if / else. Every frame, sin(time x speed) swings between -1 and 1. When it is above 0 the true wire runs and the light goes bright, otherwise the false wire runs and it goes dim. Get Light fetches the Light component of this entity." />
      <data key="w" value="960" />
    </node>
    <node id="6" type="math.multiply" x="180" y="170" />
    <node id="7" type="math.sin" x="340" y="170" />
    <node id="10" type="var.get:bright" x="617" y="224" />
    <node id="12" type="var.get:dim" x="617" y="304" />
    <node id="4" type="api:Timer.GetTimeSec" x="0" y="150" />
    <node id="5" type="var.get:speed" x="4" y="259" />
    <node id="8" type="compare.greater" x="349" y="60" />
    <node id="3" type="flow.branch" x="587" y="-35" />
    <node id="9" type="component:Light" x="617" y="134" />
    <node id="11" type="api:Light.SetIntensity(number)" x="830" y="-36" />
    <node id="13" type="api:Light.SetIntensity(number)" x="877" y="213" />
    <node id="2" type="event.Tick" x="11" y="-35" />
  </nodes>
  <links>
    <link from="2" from_pin="then" to="3" to_pin="exec" />
    <link from="4" from_pin="result" to="6" to_pin="a" />
    <link from="5" from_pin="speed" to="6" to_pin="b" />
    <link from="6" from_pin="result" to="7" to_pin="x" />
    <link from="7" from_pin="result" to="8" to_pin="a" />
    <link from="8" from_pin="result" to="3" to_pin="condition" />
    <link from="3" from_pin="true" to="11" to_pin="exec" />
    <link from="3" from_pin="false" to="13" to_pin="exec" />
    <link from="9" from_pin="light" to="11" to_pin="target" />
    <link from="9" from_pin="light" to="13" to_pin="target" />
    <link from="10" from_pin="bright" to="11" to_pin="value" />
    <link from="12" from_pin="dim" to="13" to_pin="value" />
  </links>
</visual_script>
]==]

local script = {}
script.speed = 3
script.bright = 12000
script.dim = 300

-- per entity state the graph keeps between frames, out of the inspector's sight
local vs = {}

function script:Tick(entity)
    vs.delta = Timer.GetDeltaTimeSec()
    -- Branch [3]
    if (math.sin((Timer.GetTimeSec() * self.speed)) > 0) then
        -- Set Intensity (number) [11]
        (entity:GetComponent(ComponentType.Light)):SetIntensity(self.bright)
    else
        -- Set Intensity (number) [13]
        (entity:GetComponent(ComponentType.Light)):SetIntensity(self.dim)
    end
end

return script
