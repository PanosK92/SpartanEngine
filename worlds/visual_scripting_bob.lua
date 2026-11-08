-- A visual script. Spartan generated this file from the graph below, edit the graph in the editor's Visual Script window,
-- changes made to this file by hand are replaced the next time the graph is saved.
--[==[spartan-visual-script
<visual_script version="1" next_id="20">
  <view x="100" y="263.245148" zoom="0.718773246" />
  <variables>
    <variable name="height" type="number" value="0.25" />
    <variable name="speed" type="number" value="2" />
    <variable name="base_y" type="number" value="0" />
  </variables>
  <nodes>
    <node id="1" type="util.comment" x="-20" y="-170">
      <data key="h" value="120" />
      <data key="text" value="LESSON 2, VARIABLES&#10;Variables live on the entity and show up in its Properties, so you can tune them without touching the graph. On Start remembers where the sphere began in base_y. On Tick moves it to base_y + sin(time x speed) x height." />
      <data key="w" value="1060" />
    </node>
    <node id="2" type="event.Start" x="0" y="0" />
    <node id="3" type="api:Entity.GetPosition" x="0" y="110" />
    <node id="4" type="break:Vector3" x="200" y="110" />
    <node id="5" type="var.set:base_y" x="420" y="0" />
    <node id="6" type="event.Tick" x="0" y="300" />
    <node id="7" type="api:Timer.GetTimeSec" x="0" y="440" />
    <node id="8" type="var.get:speed" x="20" y="520" />
    <node id="9" type="math.multiply" x="200" y="460" />
    <node id="10" type="math.sin" x="380" y="460" />
    <node id="11" type="var.get:height" x="380" y="560" />
    <node id="12" type="math.multiply" x="560" y="480" />
    <node id="13" type="var.get:base_y" x="560" y="400" />
    <node id="14" type="math.add" x="740" y="420" />
    <node id="15" type="api:Entity.GetPosition" x="560" y="300" />
    <node id="16" type="break:Vector3" x="740" y="300" />
    <node id="17" type="api:Vector3.new(number,number,number)" x="920" y="340" />
    <node id="18" type="api:Entity.SetPosition" x="1240" y="300" />
  </nodes>
  <links>
    <link from="2" from_pin="then" to="5" to_pin="exec" />
    <link from="3" from_pin="result" to="4" to_pin="value" />
    <link from="4" from_pin="y" to="5" to_pin="base_y" />
    <link from="6" from_pin="then" to="18" to_pin="exec" />
    <link from="7" from_pin="result" to="9" to_pin="a" />
    <link from="8" from_pin="speed" to="9" to_pin="b" />
    <link from="9" from_pin="result" to="10" to_pin="x" />
    <link from="10" from_pin="result" to="12" to_pin="a" />
    <link from="11" from_pin="height" to="12" to_pin="b" />
    <link from="13" from_pin="base_y" to="14" to_pin="a" />
    <link from="12" from_pin="result" to="14" to_pin="b" />
    <link from="15" from_pin="result" to="16" to_pin="value" />
    <link from="16" from_pin="x" to="17" to_pin="value 1" />
    <link from="14" from_pin="result" to="17" to_pin="value 2" />
    <link from="16" from_pin="z" to="17" to_pin="value 3" />
    <link from="17" from_pin="result" to="18" to_pin="position" />
  </links>
</visual_script>
]==]

local script = {}
script.height = 0.25
script.speed = 2
script.base_y = 0

-- per entity state the graph keeps between frames, out of the inspector's sight
local vs = {}

function script:Start(entity)
    -- Set base_y [5]
    self.base_y = (entity:GetPosition()).y
end

function script:Tick(entity)
    vs.delta = Timer.GetDeltaTimeSec()
    -- Set Position [18]
    entity:SetPosition(Vector3((entity:GetPosition()).x, (self.base_y + (math.sin((Timer.GetTimeSec() * self.speed)) * self.height)), (entity:GetPosition()).z))
end

return script
