* install
  ```shell
  git clone https://github.com/ycpedef/socket-stg --depth=1
  # or
  # git clone https://gitee.com/yuanchenpu/socket-stg --depth=1
  cd socket-stg
  make
  ```
* run  
  1. run `./server` in one terminal
  2. run `./client [server_ip]` in another terminal

* instructions  
  forked from [this repository](https://github.com/wierton/socket-based-naive-game)

  1. use w s a d j k to switch selected button.
  2. type `<TAB>` to enter command mode.
	* type `help --list` for all available commands
	* type `help command` for further information of this command
  3. character in battle

	|  character  |  meaning  |
	|-------------|-----------|
	|      Y      |    you    |
	|      A      |   others  |
	|      █      |   grass   |
	|      X      |   magma   |
	|      +      |  magazine |
	|      *      | blood vial|
  4. operations in battle
    * w s a d can move around
	* k j h l can fire
  5. quit the battle
    * note that even you die, you won't be quited from the battle
	but your role will be changed from player into witness. If you
	want to return the last ui, you need to type `q`.
