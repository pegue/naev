--[[

   Simple cargo mission that opens up the Empire cargo missions.

]]--

lang = naev.lang()
if lang == "es" then
   -- not translated atm
else -- default english
   misn_title = "Empire Recruitment"
   misn_reward = "%d credits"
   misn_desc = "Deliver some parcels for the Empire to %s in %s."
   title = {}
   title[1] = "Spaceport Bar"
   title[2] = "Empire Recruitment"
   title[3] = "Mission Accomplished"
   text = {}
   text[1] = [[As you enter the bar you can't help to notice that a fellow at a table is looking at you since you came in.  You tend to your business as if you hadn't noticed.  A while later you feel a tap on your shoulder and see it's the same fellow.]]
   text[2] = [["Hello, sorry about interrupting you.  I'm Lieutenant Czesc from the Empire Armada Shipping Division.  We're having another recruitment operation and would be interested in having another pilot among us.  Would be interesting in working for the Empire?"]]
   text[3] = [["Welcome aboard.", says Czesc before giving you a firm handshake.  "At first you'll just be tested with cargo missions while we get data on your flying skills.  Later you could get called for more important mission.  Who knows?  You could be the next Yao Pternov, greatest pilot we ever had on the armada."
He hits a couple buttons on his wrist computer that springs into action.
"It looks like we already have a simple task for you. Deliver these parcels to %s.  The best pilots started delivering papers and ended up flying into combat against gigantic warships with the Interception Division."]]
   text[4] = [[You deliver the parcels to the Empire Shipping station at the %s spaceport.  Afterwards they make you do some paperwork to formalize your participation with the Empire.  They tell you to keep an eye out for missions labeled ES in the mission computer which stand for Empire Shipping which you now have access to.
You aren't too sure of what to make of your encounter with the Empire, only time will tell...]]
end


function create()

   local landed, landed_sys = planet.get()

   -- target destination
   local i = 0
   repeat
      dest,sys = planet.get( misn.factions() )
      i = i + 1
   until sys ~= landed_sys or i > 10
   -- infinite loop protection
   if i > 10 then
      misn.finish(false)
   end
   misn.setMarker(sys)

   -- Intro text
   tk.msg( title[1], text[1] )
   if tk.yesno( title[1], text[2] )
      then
      misn.accept()

      -- Mission details
      reward = 3000
      misn.setTitle(misn_title)
      misn.setReward( string.format(misn_reward, reward) )
      misn.setDesc( string.format(misn_desc,dest:name(),sys:name()))

      -- Flavour text and mini-briefing
      tk.msg( title[2], string.format( text[3], dest:name() ))

      -- Set up the goal
      parcels = misn.addCargo("Parcels", 0)
      hook.land("land")
   end
end


function land()

   local landed = planet.get()
   if landed == dest then
      if misn.rmCargo(parcels) then
         player.pay(reward)
         -- More flavour text
         tk.msg(title[3], string.format( text[4], dest:name() ))
         var.push("es_cargo", true)
         player.modFaction("Empire",3);
         misn.finish(true)
      end
   end
end

