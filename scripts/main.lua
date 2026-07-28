-- main.lua — third-person cube + click-to-move

local cubeId = 0
local cubeVel = {x = 0, y = 0, z = 0}
local onGround = false
local targetPos = nil
local moveSpeed = 5.0

local camYaw = 0.0
local camPitch = 0.35
local camDist = 8.0
local clickFrame = -1
local frame = 0

function setup()
    renderer.set_time_of_day(0.45)
    renderer.set_atmosphere(true)
    renderer.set_rain(true, 0.25)
    renderer.set_clouds(true)
    renderer.set_cloud_coverage(0.5)

    for x = -8, 8, 2 do
        for z = 2, 12, 2 do
            scene.spawn_cube(x, 0, z, 0.5)
        end
    end

    cubeId = ecs.create({"Transform"})
    ecs.set_position(cubeId, 0, 0.5, 5)

    local f = io.open("lua_log.txt", "w")
    if f then f:write("READY\n"); f:close() end
end

function update(dt)
    frame = frame + 1
    if frame % 120 == 0 then
        local f = io.open("lua_log.txt", "a")
        local x, y, z = ecs.get_position(cubeId)
        if f then f:write(string.format("F=%d camDist=%.1f pos=(%.1f,%.1f,%.1f)\n", frame, camDist, x, y, z)); f:close() end
    end

    -- INPUT
    local mx, my = input.mouse_position()
    local md_left = input.mouse_down(0)
    local md_right = input.mouse_down(1)
    local mh_right = input.mouse_held(1)
    local dx, dy = input.mouse_delta()
    local scroll = input.scroll()

    -- ORBITA camera com RMB
    if mh_right then
        camYaw = camYaw - dx * 0.005
        camPitch = camPitch - dy * 0.005
        if camPitch > 1.2 then camPitch = 1.2 end
        if camPitch < -1.2 then camPitch = -1.2 end
    end

    -- ZOOM com scroll
    camDist = camDist - scroll * 2.0
    if camDist < 3 then camDist = 3 end
    if camDist > 30 then camDist = 30 end

    -- LEFT CLICK = move to point
    if md_left and clickFrame ~= frame then
        clickFrame = frame
        local ox, oy, oz, dxr, dyr, dzr = physics.screen_ray(mx, my, 1920, 1080)
        -- raio vs plano y=0
        if dyr < -0.01 then
            local t = -oy / dyr
            if t > 0 then
                targetPos = {
                    x = ox + dxr * t,
                    y = 0.5,
                    z = oz + dzr * t
                }
            end
        end
    end

    -- MOVE cube toward target
    local cx, cy, cz = ecs.get_position(cubeId)
    if targetPos then
        local d = ((cx - targetPos.x)*(cx - targetPos.x) + (cz - targetPos.z)*(cz - targetPos.z)) ^ 0.5
        if d < 0.3 then
            targetPos = nil
        else
            local dx2 = targetPos.x - cx
            local dz2 = targetPos.z - cz
            local mag = (dx2*dx2 + dz2*dz2) ^ 0.5
            if mag > 0.001 then
                cx = cx + (dx2 / mag) * moveSpeed * dt
                cz = cz + (dz2 / mag) * moveSpeed * dt
                ecs.set_position(cubeId, cx, 0.5, cz)
            end
        end
    end

    -- JUMP com SPACE
    local spaceDown = input.key_down("Space")
    if spaceDown and onGround then
        cubeVel.y = 10.0
        onGround = false
    end
    cubeVel.y = cubeVel.y - 20.0 * dt
    cy = cy + cubeVel.y * dt
    if cy <= 0.5 then cy = 0.5; cubeVel.y = 0; onGround = true end
    ecs.set_position(cubeId, cx, cy, cz)

    -- CAMERA third-person
    local px, py, pz = ecs.get_position(cubeId)
    local camX = px + camDist * math.sin(camYaw) * math.cos(camPitch)
    local camY = py + camDist * math.sin(camPitch)
    local camZ = pz + camDist * math.cos(camYaw) * math.cos(camPitch)
    camera.set_position(camX, camY, camZ)
    camera.look_at(px, py + 0.5, pz)
end
