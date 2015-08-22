#monkey_ai

##Compile
Place the monkey\_ai folder in the asss/src folder or symlink it there.  
`make clean && make`  

##Install
###Automatic
Add to conf/modules.conf:  

    monkey_ai:pathing
    monkey_ai:weapons  
    monkey_ai:ai  
    monkey_ai:zombies  
	
Add `pathing weapons ai zombies` to Modules:AttachModules in the arena conf.

###Manual

    ?insmod monkey_ai:pathing  
    ?attmod pathing  
    ?insmod monkey_ai:weapons  
    ?attmod weapons  
    ?insmod monkey_ai:ai  
    ?attmod ai  
    ?insmod monkey_ai:zombies  
    ?attmod zombies   

