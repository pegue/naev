include("ai/tpl/generic.lua")

-- Settings
aggressive     = true
safe_distance  = 300
armour_run     = 100
shield_return  = 20
land_planet    = false


function create ()

   -- Give monies.
   ai.setcredits(ai.shipprice()/600 , ai.shipprice()/100 )

   -- Get standing.
   p = ai.getPlayer()
   if ai.exists(p) then
      standing = ai.getstanding( p ) or -1
   else
      standing = -1
   end

   -- Handle bribes.
   if standing < -30 then
      mem.bribe_no = "\"The only way to deal with scum like you is with cannons!\""
   else
      mem.bribe = math.sqrt( ai.shipmass() ) * (300. * rnd.int() + 850.)
      mem.bribe_prompt = string.format("\"It'll cost you %d credits for me to ignore your dirty presence.\"", mem.bribe)
      mem.bribe_paid = "\"Begone before I change my mind.\""
   end

   -- Handle refueling.
   if standing > 70 then
      mem.refuel = rnd.rnd( 1000, 2000 )
      mem.refuel_msg = string.format("\"I should be able to spare some fuel for %d credits.\"", mem.refuel)
   else
      mem.refuel_no = "\"I can't spare fuel for you.\""
   end

   -- Choose attack.
   attack_choose()
end


function taunt ( target, offense )

   -- Only 50% of actually taunting.
   if rnd.int(0,1) == 0 then
      return
   end

   -- some taunts
   if offense then
      taunts = {
            "For the Frontier!",
            "You'll make great target practice!",
            "Purge the oppressors!"
      }
   else
      taunts = {
            "You are no match for the FLF.",
            "I've killed scum much more dangerous than you."
      }
   end

   ai.comm(target, taunts[ rnd.int(1,#taunts) ])
end

