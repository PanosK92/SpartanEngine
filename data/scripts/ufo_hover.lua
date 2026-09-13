-- Copyright(c) 2015-2026 Panos Karabelas. Distributed under the MIT license.
-- Distant observation, unstable hovering, and brief cinematic jumps between vantage points.
local ufo = {
    hover_height = 2.5, hover_speed = .8, rotation_speed = .015,
    follow_distance = 1700, minimum_distance = 1100, watched_distance = 2800,
    follow_altitude = 650, altitude_clearance = 350, zap_duration = .16
}
local function clamp(x,lo,hi) return math.max(lo,math.min(hi,x)) end
local function length(x,z) return math.sqrt(x*x+z*z) end
local function delta_angle(a,b) return (a-b+math.pi)%(2*math.pi)-math.pi end
local function ground_height(x,z,fallback,entity)
    local hit = World.Raycast(Vector3(x,math.max(2400,fallback+1400),z),Vector3(0,-1,0),6000)
    if hit and hit.entity ~= entity and not hit.entity:IsDescendantOf(entity) then return hit.position.y end
    return fallback
end
local function target(self,entity,camera,withdraw)
    local player,forward = camera:GetPosition(),camera:GetForward()
    local angle = math.atan(-forward.z,-forward.x)+self.side*(.6+math.random()*.6)
    local radius = (withdraw and self.watched_distance or self.follow_distance)*(.94+math.random()*.12)
    local x,z = player.x+math.cos(angle)*radius,player.z+math.sin(angle)*radius
    local y = math.max(player.y+self.follow_altitude,ground_height(x,z,player.y,entity)+self.altitude_clearance)
    return Vector3(x,y,z),player
end
local function hold(self,position)
    self.anchor_x,self.anchor_y,self.anchor_z = position.x,position.y,position.z
    self.hover_time = 12+math.random()*12
    self.zap_time = nil
end
local function begin_zap(self,entity,camera,withdraw)
    self.side = -self.side
    local destination,player = target(self,entity,camera,withdraw)
    local p = entity:GetPosition()
    local dx,dz = p.x-player.x,p.z-player.z
    self.center_x,self.center_z = player.x,player.z
    self.start_radius = length(dx,dz)
    self.start_angle = math.atan(dz,dx)
    self.end_radius = length(destination.x-player.x,destination.z-player.z)
    self.angle_change = delta_angle(math.atan(destination.z-player.z,destination.x-player.x),self.start_angle)
    self.start_y,self.end_y = p.y,destination.y
    -- Check the arc, not a straight chord that might cross close to the player.
    self.arc_clearance = math.max(p.y,destination.y)
    for i=1,6 do
        local t=i/6
        local angle=self.start_angle+self.angle_change*t
        local radius=self.start_radius+(self.end_radius-self.start_radius)*t
        self.arc_clearance=math.max(self.arc_clearance,
            ground_height(player.x+math.cos(angle)*radius,player.z+math.sin(angle)*radius,player.y,entity)+self.altitude_clearance)
    end
    self.zap_time,self.watch_time = 0,0
end
local function orient(self,entity)
    -- Small irregular pitch/roll while hovering, almost level during a jump.
    local strength = self.zap_time and .12 or 1
    local t=self.elapsed_time
    local pitch=math.rad(1.6)*(math.sin(t*.83)+.25*math.sin(t*1.91))*strength
    local roll=math.rad(2.1)*(math.cos(t*.67)+.2*math.sin(t*1.37))*strength
    local yaw=t*self.rotation_speed
    local sx,cx=math.sin(pitch*.5),math.cos(pitch*.5)
    local sy,cy=math.sin(yaw*.5),math.cos(yaw*.5)
    local sz,cz=math.sin(roll*.5),math.cos(roll*.5)
    local x,y,z,w=sx*cy*cz+cx*sy*sz,cx*sy*cz-sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz
    local q=Quaternion()
    q.x=self.bw*x+self.bx*w+self.by*z-self.bz*y
    q.y=self.bw*y-self.bx*z+self.by*w+self.bz*x
    q.z=self.bw*z+self.bx*y-self.by*x+self.bz*w
    q.w=self.bw*w-self.bx*x-self.by*y-self.bz*z
    entity:SetRotation(q)
end
function ufo.Initialize(self,entity)
    self.elapsed_time,self.watch_time,self.side=0,0,1
    self.zap_time=nil
    self.anchor_x=nil
end
function ufo.Start(self,entity)
    self:Initialize(entity)
    local rotation=entity:GetRotation()
    self.bx,self.by,self.bz,self.bw=rotation.x,rotation.y,rotation.z,rotation.w
    local camera=World.GetCameraEntity()
    if not camera then return end
    -- Acquire an unseen high vantage on play start; no edit-mode transform changes.
    local position=target(self,entity,camera,false)
    entity:SetPosition(position)
    hold(self,position)
end
function ufo.Tick(self,entity)
    local camera=World.GetCameraEntity()
    if not camera then return end
    if not self.anchor_x then self:Start(entity) end
    local dt=clamp(Timer.GetDeltaTimeSec(),0,.05)
    self.elapsed_time=self.elapsed_time+dt
    local player,forward=camera:GetPosition(),camera:GetForward()
    local p=entity:GetPosition()
    if self.zap_time then
        self.zap_time=self.zap_time+dt
        local t=clamp(self.zap_time/math.max(.05,self.zap_duration),0,1)
        local angle=self.start_angle+self.angle_change*t
        local radius=self.start_radius+(self.end_radius-self.start_radius)*t
        p.x=self.center_x+math.cos(angle)*radius
        p.z=self.center_z+math.sin(angle)*radius
        -- Rise above any intervening hill during the very brief relocation.
        p.y=self.start_y+(self.end_y-self.start_y)*t+math.sin(t*math.pi)*math.max(0,self.arc_clearance-math.min(self.start_y,self.end_y))
        entity:SetPosition(p)
        if t>=1 then hold(self,p) end
    else
        local dx,dy,dz=p.x-player.x,p.y-player.y,p.z-player.z
        local distance=length(dx,dz)
        local viewed=(dx*forward.x+dy*forward.y+dz*forward.z)/math.max(.001,math.sqrt(dx*dx+dy*dy+dz*dz))>.96
        self.watch_time=viewed and self.watch_time+dt or math.max(0,self.watch_time-2*dt)
        self.hover_time=self.hover_time-dt
        local too_close=distance<self.minimum_distance or p.y<player.y+self.follow_altitude*.75
        if self.watch_time>.7 or too_close or distance>self.watched_distance+700 or self.hover_time<=0 then
            begin_zap(self,entity,camera,self.watch_time>.7 or too_close)
        else
            local t=self.elapsed_time*self.hover_speed
            p.x=self.anchor_x+math.sin(t)*.8+math.sin(t*1.7)*.3
            p.z=self.anchor_z+math.cos(t*.7)*.8
            p.y=self.anchor_y+math.sin(t)*self.hover_height+math.sin(t*1.9)*.4
            entity:SetPosition(p)
        end
    end
    orient(self,entity)
end
return ufo
