# Ask DeepSeek ( `ads` )

`ads` is **A CLI tool** that can interact with [`DeepSeek`](https://github.com/deepseek-ai/DeepSeek-V3).
It gaves you the ability to interact with `DeepSeek` from the command line.

## What is Ask-DeepSeek?

Similar with **Jyy**'s `ag` (**a**sk **g**pt) shown in the course "**NJU Operating System**", you can use `ads` (**a**sk **d**eep**s**eek) to ask questions and get answers with `DeepSeek` in the terminal.


## How to use Ask-DeepSeek?


```bash
$ ads "Hello DeepSeek, how are you?"
```


### Basic Usage

```
Usage: ads [OPTION]... "<QUESTION>"
Command-line interface for DeepSeek LLM API

Options:
  -p, --print-env                 Print current configuration and exit
  -j, --just-json                 Generate request JSON without sending to API
  -c, --count-token               Show token usage statistics
  -e, --echo                      Echo the user's input question
  -h, --help                      Display this help and exit

Examples:
    ads -p                        # Show current config   
    ads -j -e "Your question"    # Generate JSON and echo input
```

### <span id="jump1">Configuration (`.adsenv`)</span>

The `adsenv` file is a configuration file that allows you to set the default values for the `ads` command.
The program will look for this file in the following locations sequentially:
1. The current directory: `./.adsenv`
2. The user's home directory: `~/.adsenv`
3. The user's configuration directory: `~/.config/.adsenv`
4. The system-wide configuration directory: `/etc/ads/.adsenv`

The elements in the configuration file include `API-KEY`, `BASE_URL`, `MODEL`, `SYSTEM_MSG`. The configuration file looks like this:

```bash
API_KEY=sk-ed......................293af
BASE_URL=https://api.deepseek.com/chat/completions
MODEL=deepseek-chat
SYSTEM_MSG="You are a professional of Computer Science."
```


## How to install Ask-DeepSeek?

***!!! <u>Before you install the program, a `.adsenv` file is nascessary.</u>***
See Section: "[Configuration (`.adsenv`)](#jump1)"

### Requirements

The program requires the following dependencies to be installed on your system:
- `cJSON`: A JSON parser for C
- `libcurl`: A library for transferring data with URLs

#### Ubuntu/Debian

```bash
sudo apt update
sudo apt install libcjson-dev
sudo apt install libcurl4-openssl-dev
```

#### Fedora

```bash
sudo dnf install cjson-devel
sudo dnf install libcurl-devel
```

#### macOS (Homebrew)

```bash
brew install cjson
brew install curl
```

#### Check the installation

You can check the installation of the libraries by running the following commands:

```bash
pkg-config --cflags --libs libcjson
pkg-config --cflags --libs libcurl
```

If the installation is successful, you should see content similar to the following:

```bash
-I/usr/include/cjson -lcjson
-I/usr/include/x86_64-linux-gnu/curl -lcurl
```

### Compile from source

If you want to compile the program from source, you can follow the instructions below: *（Remember to install the dependencies first）*

```bash
$ git clone https://github.com/rouge3877/Ask-DeepSeek.git
$ cd Ask-DeepSeek
$ vim .adsenv # Create the .adsenv file and add the configuration
$ make release
```

After running the above commands, you should see the `ads` executable in the `./build/release` directory.
You can also use the `make debug` command to compile the program in debug mode. Use `make help` to see all available options.

### Install from source

You can also install the program on your system by running the following command:

```bash
$ git clone https://github.com/rouge3877/Ask-DeepSeek.git
$ cd Ask-DeepSeek
$ vim .adsenv # Create the .adsenv file and add the configuration
$ sudo make install
```

After running the above commands, you should be able to use the `ads` command from the terminal.

### Uninstall

If you want to uninstall the program from your system, you can run the following command:

```bash
$ sudo make uninstall
```

## How to get the API key?

You can get the API key by signing up on the [DeepSeek](https://platform.deepseek.com/). After signing up, you can get the API key from the dashboard.



## TODO
- [x] Resolve bugs in output animation.
- [x] Add functionality to use the stream parameter in the API.
- [ ] Support more configurations in the `.adsenv` file.
- [ ] Support read question from `stdin`
- [x] Split a single file in the project into different files to increase readability and maintainability.
