--[[

   Nebulae Satellite

   One-shot mission

   Help some independent scientists put a satellite in the nebulae.

]]--

-- localization stuff, translators would work here
lang = naev.lang()
if lang == "es" then
else -- default english
   mtitle = {}
   mtitle[1] = "Nebulae Satellite"
   mreward = {}
   mreward[1] = "%d credits"
   mdesc = {}
   mdesc[1] = "Go to the %s system to launch the probe."
   mdesc[2] = "Drop off the scientists at %s in the %s system."
   title = {}
   title[1] = "Bar"
   title[2] = "Scientific Exploration"
   title[3] = "Mission Success"
   text = {}
   text[1] = [[You are sitting at the bar when you are approached by a couple of short guys.  They seem a bit nervous and one mutters something about whether it's a good idea or not.  Eventually one of them comes up to you.
"Hello Captain, we're looking for a ship to take us into the Sol Nebulae.  Would you be willing to take us there?"]]
   text[2] = [["We had a trip scheduled with some Space Trader ship, but they backed out at the last minute.  So we were stuck here until you came.  We've got probe satellite that we have to release in the %s system to monitor the nebula's growth rate.  The probe launch procedure is pretty straightforward and shouldn't have any complications."
He takes a deep breath, "We hope to be able to find out more secrets of the Sol Nebulae so man can once again regain it's lost patrimony.  So far the radiation and volatility of the deeper areas haven't been very kind to our instruments.  That's why we designed this Satellite we're going to launch."]]
   text[3] = [["The plan is for you to take us to %s so we can launch the probe then back to our home at %s in the %s system.  The probe will automatically send us the data we need if all goes well.  You'll be paid %d credits when we arrive."]]
   text[4] = [[The scientists thank you for your help before going back to their home to continue their nebulae research.]]
   text[9] = [["You do not have enough free cargo space to accept this mission!"]]
   launch = {}
   launch[1] = "Preparing to launch satellite probe..."
   launch[2] = "Launch in 5..."
   launch[3] = "Satellite launch successful!"
end


function create()

   if tk.yesno( title[1], text[1] )
      then

      -- Check for cargo space
      if player.freeCargo() <  3 then
         tk.msg( title[1], text[9] )
         return
      end
      misn.accept()

      -- Set up mission variables
      misn_stage = 0
      homeworld, homeworld_sys = planet.get( misn.factions() )
      satellite_sys = system.get("Arandon") -- Not too unstable
      credits = 75000
      cargo = misn.addCargo( "Satellite", 3 )

      -- Set up mission information
      misn.setTitle( mtitle[1] )
      misn.setReward( string.format( mreward[1], credits ) )
      misn.setDesc( string.format( mdesc[1], satellite_sys:name() ) )
      misn.setMarker( satellite_sys )

      -- More flavour text
      tk.msg( title[2], string.format(text[2], satellite_sys:name()) )
      tk.msg( title[2], string.format(text[3], satellite_sys:name(),
            homeworld:name(), homeworld_sys:name(), credits ) )

      -- Set up hooks
      hook.land("land")
      hook.enter("jump")
   end
end


function land ()
   landed = planet.get()
   -- Mission success
   if misn_stage == 1 and landed == homeworld then
      tk.msg( title[3], text[4] )
      player.pay( credits )
      misn.finish(true)
   end
end


function jump ()
   sys = system.get()
   -- Launch satellite
   if misn_stage == 0 and sys == satellite_sys then
      misn.timerStart( "beginLaunch", 3000 )
   end
end


--[[
   Launch process
--]]
function beginLaunch ()
   player.msg( launch[1] )
   misn.timerStart( "beginCountdown", 3000 )
end
function beginCountdown ()
   countdown = 5
   player.msg( launch[2] )
   misn.timerStart( "countLaunch", 1000 )
end
function countLaunch ()
   countdown = countdown - 1
   if countdown <= 0 then
      launchSatellite()
   else
      player.msg( string.format("%d...", countdown) )
      misn.timerStart( "countLaunch", 1000 )
   end
end
function launchSatellite ()
   misn_stage = 1
   player.msg( launch[3] )
   misn.jetCargo( cargo )
   misn.setDesc( string.format( mdesc[2], homeworld:name(), homeworld_sys:name() ) )
   misn.setMarker( homeworld_sys )
end
