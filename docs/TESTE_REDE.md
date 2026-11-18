# Validação da D1 em dois computadores

Conecte dois computadores Linux à mesma rede Wi-Fi ou ao mesmo hotspot. O teste usa o protocolo UDP da D1 pela porta 42917 e roda pelo terminal, independentemente do menu do jogo.

## Preparação

Na raiz do repositório, em cada computador, rode:

```bash
git fetch origin daniel/d1-rede
git switch --detach FETCH_HEAD
g++ -std=c++17 -Wall -Wextra -pthread -Isrc tests/birds-lan.cpp src/net/UdpSession.cpp -o /tmp/birds-lan
```

## Descoberta e troca de mensagens

1. No computador A, execute `/tmp/birds-lan host` e deixe o programa rodando.
2. No computador B, execute `/tmp/birds-lan discover`. A sala deve aparecer com o nome `Birds D1` e o endereço do host, por exemplo `192.168.1.42:42917`.
3. No B, execute `/tmp/birds-lan join 192.168.1.42:42917`, substituindo o endereço pelo encontrado.
4. Confira se A exibe `Recebido 1`, `Recebido 2` e `Recebido 3`, e se B exibe `Eco recebido 1`, `Eco recebido 2` e `Eco recebido 3`, em ordem e sem duplicatas.

Se a descoberta falhar, obtenha o IPv4 da rede com `hostname -I` no A e tente o comando `join` com esse endereço. Se funcionar, registre a conexão por IP como aprovada e a descoberta como pendente naquela rede. Se ambos falharem, verifique se a rede permite comunicação entre os dispositivos e se o firewall do A permite UDP na porta 42917.

## Detecção de queda

Após o terceiro eco, desative a conexão de rede do B. O computador A deve exibir `Desconexão detectada` em cerca de quatro segundos. Para repetir o teste, reconecte o B e reinicie o programa nos dois computadores.
