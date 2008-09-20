include("ai/tpl/generic.lua")

-- Simple create function
function create ()
   attack_choose()
end

-- Just tries to guard mem.escort
function idle ()
   ai.pushtask(0, "escort")
end

-- Escorts the target
function escort ()
   target = mem.escort

   -- Will just float without a target to escort.
   if target == nil then
      return
   end
   
   dir = ai.face(target)
   dist = ai.dist( ai.pos(target) )
   bdist = ai.minbrakedist()

   -- Brake
   if not ai.isstopped() and dist < bdist then
      ai.pushtask(0, "brake")

   -- Must approach
   elseif dir < 10 and dist > 200 then
      ai.accel()

   end
end

-- Just brakes
function brake ()
   ai.brake()
   if ai.isstopped() then
      ai.poptask()
   end
end


-- Holds position
function hold ()
   if not ai.isstopped() then
      ai.brake()
   end
end


-- Tries to fly back to carrier
function flyback ()
   target = mem.escort

   dir = ai.face(target)
   dist = ai.dist( ai.pos(target) )
   bdist = ai.minbrakedist()

   -- Try to brake
   if not ai.isstopped() and dist < bdist then
      ai.pushtask(0, "brake")

   -- Try to dock
   elseif ai.isstopped() and dist < 30 then
      ai.dock(target)

   -- Far away, must approach
   elseif dir < 10 and dist > 200 then
      ai.accel()
   end
end


--[[
--    Escort commands
--]]
-- Attack target
function e_attack( target )
   if mem.command then
      ai.pushtask(0, "attack", target)
   end
end
-- Hold position
function e_hold ()
   if mem.command then
      ai.pushtask(0, "hold" )
   end
end
-- Return to carrier
function e_return ()
   if mem.command and mem.carrier then
      ai.pushtask(0, "flyback" )
   end
end
-- Clear orders
function e_clear ()
   if mem.command then
      while ai.taskname ~= "none" do
         ai.poptask()
      end
   end
end