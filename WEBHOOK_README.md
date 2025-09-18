# Process Monitor Webhook Server

Este projeto foi modificado para funcionar como um servidor webhook que se comunica com o driver do kernel para obter informações de processos e enviar os resultados para uma API remota.

## Dependências

Para compilar este projeto, você precisa das seguintes bibliotecas:

### 1. cpp-httplib
- **Repositório**: https://github.com/yhirose/cpp-httplib
- **Instalação**: 
  - Baixe o arquivo `httplib.h` e coloque-o no diretório de includes do projeto
  - Ou use vcpkg: `vcpkg install cpp-httplib`

### 2. nlohmann/json
- **Repositório**: https://github.com/nlohmann/json
- **Instalação**:
  - Baixe o arquivo `json.hpp` e coloque-o no diretório de includes do projeto
  - Ou use vcpkg: `vcpkg install nlohmann-json`

### Instalação via vcpkg (Recomendado)

```bash
# Instalar vcpkg se ainda não tiver
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# Instalar as dependências
.\vcpkg install cpp-httplib:x64-windows
.\vcpkg install nlohmann-json:x64-windows

# Integrar com Visual Studio
.\vcpkg integrate install
```

## Configuração do Projeto

Após instalar as dependências, certifique-se de que o projeto está configurado para:
- **C++ Standard**: C++20 (já configurado no .vcxproj)
- **Runtime Library**: Multi-threaded Debug (já configurado)
- **Execution Level**: Require Administrator (necessário para comunicação com driver)

## Como Usar

### 1. Iniciar o Servidor

```bash
# Compile e execute o projeto
usermode.exe
```

O servidor iniciará na porta 8888 por padrão e estará disponível em `http://localhost:8888`

### 2. Endpoints Disponíveis

#### POST /webhook/list-processes
Lista todos os processos em um arquivo no sistema.
```bash
curl -X POST http://localhost:8888/webhook/list-processes
```

#### POST /webhook/process-count
Obtém o número total de processos.
```bash
curl -X POST http://localhost:8888/webhook/process-count
```

#### POST /webhook/process-by-index
Obtém informações de um processo por índice.
```bash
curl -X POST http://localhost:8888/webhook/process-by-index \
  -H "Content-Type: application/json" \
  -d '{"index": 0}'
```

#### POST /webhook/process-by-pid
Obtém informações de um processo por PID.
```bash
curl -X POST http://localhost:8888/webhook/process-by-pid \
  -H "Content-Type: application/json" \
  -d '{"pid": 1234}'
```

#### POST /webhook/iterate-processes
Obtém uma lista resumida de todos os processos.
```bash
curl -X POST http://localhost:8888/webhook/iterate-processes
```

#### POST /webhook/set-api-url
Configura a URL da API remota para onde os dados serão enviados.
```bash
curl -X POST http://localhost:8888/webhook/set-api-url \
  -H "Content-Type: application/json" \
  -d '{"apiUrl": "http://your-api-server.com/api/process-data"}'
```

#### GET /health
Verifica o status de saúde do servidor.
```bash
curl http://localhost:8888/health
```

#### GET /status
Obtém informações detalhadas sobre o status do servidor.
```bash
curl http://localhost:8888/status
```

## Formato das Respostas JSON

### Resposta de Processo Individual
```json
{
  "success": true,
  "processInfo": {
    "processName": "notepad.exe",
    "processId": 1234,
    "parentProcessId": 5678,
    "threadCount": 4,
    "handleCount": 156,
    "basePriority": 8,
    "createTime": "2024-01-15 10:30:45",
    "userTime": 1234567890,
    "kernelTime": 987654321,
    "memory": {
      "workingSetSize": 12345678,
      "peakWorkingSetSize": 23456789,
      "virtualSize": 34567890,
      "peakVirtualSize": 45678901,
      "pagefileUsage": 5678901,
      "peakPagefileUsage": 6789012,
      "pageFaultCount": 789
    },
    "io": {
      "readOperationCount": 123,
      "writeOperationCount": 456,
      "otherOperationCount": 789,
      "readTransferCount": 1234567,
      "writeTransferCount": 2345678,
      "otherTransferCount": 3456789
    },
    "currentProcessAddress": "0x12345678",
    "previousProcessAddress": "0x87654321",
    "nextProcessAddress": "0xabcdef12"
  }
}
```

### Resposta de Lista de Processos
```json
{
  "success": true,
  "processCount": 150,
  "processes": [
    {
      "index": 0,
      "processName": "System",
      "processId": 4
    },
    {
      "index": 1,
      "processName": "notepad.exe",
      "processId": 1234
    }
  ]
}
```

## Comunicação com API Remota

O servidor automaticamente envia os resultados das operações para uma API remota configurada. Por padrão, a URL é `http://localhost:8080/api/process-data`, mas pode ser alterada usando o endpoint `/webhook/set-api-url`.

### Endpoints da API Remota
- `/list-processes` - Recebe resultados de listagem de processos
- `/process-count` - Recebe contagem de processos
- `/process-by-index` - Recebe informações de processo por índice
- `/process-by-pid` - Recebe informações de processo por PID
- `/iterate-processes` - Recebe lista completa de processos

## Requisitos do Sistema

- Windows 10/11
- Visual Studio 2022 com C++20
- Driver do kernel carregado (`ExampleDriver`)
- Privilégios de administrador
- Acesso à rede (para comunicação com API remota)

## Segurança

- O servidor requer privilégios de administrador
- CORS está habilitado para todas as origens (ajuste conforme necessário)
- Considere implementar autenticação para uso em produção
- O driver do kernel deve estar assinado e carregado adequadamente

## Troubleshooting

### "Failed to open device"
- Certifique-se de que o driver do kernel está carregado
- Execute como administrador
- Verifique se o nome do dispositivo está correto (`\\.\ExampleDriver`)

### "Failed to start server"
- Verifique se a porta 8888 não está em uso
- Execute como administrador
- Verifique configurações de firewall

### Dependências não encontradas
- Instale cpp-httplib e nlohmann-json via vcpkg
- Certifique-se de que os headers estão no path de include
- Execute `vcpkg integrate install` se usando vcpkg