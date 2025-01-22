# Ask DeepSeek ( `ads` )

A cli tool to interact with the DeepSeek API.
It gaves you the ability to interact with the DeepSeek API from the command line.

## What is Ask-DeepSeek?

Similar with **Jyy**'s `ag` shown in the course "**NJU Operating System**", `ads` is a **command line tool that interacts with `DeepSeek`**. You can use it to ask questions and get answers from the `DeepSeek` in the terminal.


## How to use Ask-DeepSeek?


```bash
$ ads "Hello DeepSeek, how are you?"
```


### Basic Usage

```bash
$ ./ads "Hello, how are you?" # Ask a question
```

### Configuration (`.adsenv`)

The `adsenv` file is a configuration file that allows you to set the default values for the `ads` command.
The program will look for this file in the following locations sequentially:
1. The current directory: `./.adsenv`
2. The user's home directory: `~/.adsenv`
3. The user's configuration directory: `~/.config/.adsenv`
4. The system-wide configuration directory: `/etc/.adsenv`

The elements in the configuration file include `API-KEY`, `BASE_URL`, `MODEL`, `SYSTEM_MSG`. The configuration file looks like this:

```bash
API_KEY=sk-ed......................293af
BASE_URL=https://api.deepseek.com/chat/completions
MODEL=deepseek-chat
SYSTEM_MSG="你是一个专业的科学解释助手"
```


## How to install Ask-DeepSeek?

## TODO
- [ ] Resolve bugs in output animation.
- [ ] Add functionality to use the stream parameter in the API.
- [ ] Support providing more configurations in the. adsenv file.
- [ ] Enhance the response and handling of errors, and present error information as comprehensively as possible, while also ensuring readability. If the code is incorrect, the return value will be the error value of HTTP.
- [ ] Split a single file in the project into different files to increase readability and maintainability.