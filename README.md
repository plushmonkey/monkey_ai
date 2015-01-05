#monkey_ai

##Compile
Place the monkey\_ai folder in the asss/src folder or symlink it there.  
`make clean && make`  

##Install
###Automatic
Add to conf/modules.conf:  

    monkey_ai:pathing  
    monkey_ai:ai  
    monkey_ai:zombies  
	
Add `pathing ai zombies` to Modules:AttachModules in the arena conf.

###Manual

    ?insmod monkey_ai:pathing  
    ?attmod pathing  
    ?insmod monkey_ai:ai  
    ?attmod ai  
    ?insmod monkey_ai:zombies  
    ?attmod zombies   

##Files
monkey\_ai.h : Interface for the AI module.  
monkey\_ai.c : Implementation of a simple weapon tracking AI module for asss.  
monkey\_zombies.c : A zombies game using the AI module.  
monkey\_pathing.h : The interface for the pathing module.  
monkey\_pathing.c : Jump point search implementation.  
grid.h : The interface for a grid.  
grid.c : The implementation of the grid.  
pqueue.h : The interface for a priority queue.  
pqueue.c : The implementation of the priority queue.  
