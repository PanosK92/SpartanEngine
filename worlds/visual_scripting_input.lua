-- A visual script. Spartan generated this file from the graph below, edit the graph in the editor's Visual Script window,
-- changes made to this file by hand are replaced the next time the graph is saved.
--[==[spartan-visual-script
<visual_script version="1" next_id="10">
  <view x="130.278992" y="240" zoom="0.931999981" />
  <variables>
    <variable name="big" type="number" value="1.3" />
    <variable name="small" type="number" value="0.8" />
  </variables>
  <nodes>
    <node id="1" type="util.comment" x="-20" y="-160">
      <data key="h" value="110" />
      <data key="text" value="LESSON 3, INPUT AND FLOW&#10;On Key runs when a key goes down (pressed) or up (released), only while the game is playing. Flip Flop alternates between its a and b outputs every time it runs, so each press of Space switches the cube between big and small." />
      <data key="w" value="900" />
    </node>
    <node id="3" type="flow.flip_flop" x="220" y="0" />
    <node id="4" type="var.get:big" x="240" y="150" />
    <node id="5" type="api:Vector3.new(number,number,number)" x="420" y="110" />
    <node id="6" type="api:Entity.SetScale" x="780" y="-20" />
    <node id="7" type="var.get:small" x="240" y="330" />
    <node id="8" type="api:Vector3.new(number,number,number)" x="420" y="290" />
    <node id="9" type="api:Entity.SetScale" x="780" y="230" />
    <node id="2" type="event.key" x="-101" y="0">
      <value pin="key" value="Space" />
    </node>
  </nodes>
  <links>
    <link from="2" from_pin="pressed" to="3" to_pin="exec" />
    <link from="3" from_pin="a" to="6" to_pin="exec" />
    <link from="3" from_pin="b" to="9" to_pin="exec" />
    <link from="4" from_pin="big" to="5" to_pin="value 1" />
    <link from="4" from_pin="big" to="5" to_pin="value 2" />
    <link from="4" from_pin="big" to="5" to_pin="value 3" />
    <link from="5" from_pin="result" to="6" to_pin="scale" />
    <link from="7" from_pin="small" to="8" to_pin="value 1" />
    <link from="7" from_pin="small" to="8" to_pin="value 2" />
    <link from="7" from_pin="small" to="8" to_pin="value 3" />
    <link from="8" from_pin="result" to="9" to_pin="scale" />
  </links>
</visual_script>
]==]

local script = {}
script.big = 1.3
script.small = 0.8

-- per entity state the graph keeps between frames, out of the inspector's sight
local vs = {}

function script:Tick(entity)
    vs.delta = Timer.GetDeltaTimeSec()
    if Input.GetKeyDown(KeyCode.Space) then
        -- Flip Flop [3]
        vs.flip3 = not vs.flip3
        if vs.flip3 then
            -- Set Scale [6]
            entity:SetScale(Vector3(self.big, self.big, self.big))
        else
            -- Set Scale [9]
            entity:SetScale(Vector3(self.small, self.small, self.small))
        end
    end
end

return script
