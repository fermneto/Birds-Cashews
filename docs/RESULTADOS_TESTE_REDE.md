# Resultados do teste de rede da D1

Registro do teste feito em 18/11/2026 com a branch `daniel/d1-rede`, no commit `f98ae6d`, que depois entrou na `main`
pelo PR #6. O roteiro está em [`TESTE_REDE.md`](TESTE_REDE.md).

**Situação: parcialmente aprovado.** As mensagens do cliente chegaram ao host entre dois computadores, e o host detectou
a saída do cliente. Ainda faltam as confirmações listadas na seção 6.

## 1. Ambiente

| Item | Valor |
|---|---|
| Host | Computador do Neto |
| Cliente | Computador do Daniel |
| Rede | Eduroam |
| Programa | `tests/birds-lan.cpp`, compilado com `src/net/UdpSession.cpp` |
| Protocolo e porta | UDP, porta 42917 |

## 2. Teste em um computador

Host e cliente rodaram na máquina do Neto. O cliente foi encerrado à força, sem avisar o host.

| Verificação | Resultado |
|---|---|
| Compilação do programa de teste | Aprovada |
| Descoberta da sala `Birds D1` | Aprovada |
| Entrada na sala por IP e porta | Aprovada |
| Host recebe as mensagens 1, 2 e 3, em ordem | Aprovada |
| Cliente recebe os ecos 1, 2 e 3, em ordem | Aprovada |
| Host detecta a queda do cliente | Aprovada |

## 3. Teste entre dois computadores

O log do host registrou:

```text
Sala aberta
Recebido 1
Recebido 2
Recebido 3
Desconexão detectada
HOST_EXIT=0
```

| Verificação | Resultado |
|---|---|
| Mensagens do cliente chegam ao host, em ordem e sem duplicatas | Aprovada |
| Host detecta a saída do cliente e encerra sem erro | Aprovada |
| Cliente recebe os ecos do host | Não confirmada |
| Descoberta automática da sala | Não confirmada |
| Queda real, com o Wi-Fi desligado | Não confirmada |

A tela do cliente não foi registrada. Por isso, os ecos e a descoberta dependem da confirmação do Daniel. O log do host
também não mostra se o cliente saiu por queda de rede ou por encerramento do programa.

## 4. Testes automáticos de rede

`runNetworkSelfTest` faz parte de `./birds --selftest` desde a integração da D4. Todas as verificações passaram em
18/11/2026:

| Verificação | Resultado |
|---|---|
| Descoberta local da sala | Aprovada |
| Entrada por IP usando a porta padrão | Aprovada |
| Host atribui o ID do jogador | Aprovada |
| Mensagem entra na fila de envio | Aprovada |
| Mensagem e confirmação perdidas são reenviadas, sem duplicar e em ordem | Aprovada |
| Queda detectada sem pacote de saída | Aprovada |
| Host remove a sessão expirada | Aprovada |

## 5. Jogo completo entre dois computadores

Em 19/11/2026, o Neto e um amigo jogaram o Birds&Cashews em dois computadores diferentes, usando a branch
`integracao/rede-voz` (commit `a4a693e`). A conexão, a sala e a voz por proximidade funcionaram. A rede usada e os
detalhes de cada verificação não foram registrados.

## 6. Pendências

1. Repetir a queda desligando o Wi-Fi do cliente.
2. Repetir o roteiro completo no hotspot do celular, que é a rede da apresentação.
3. Testar com mais de dois jogadores conectados.

## 7. Validação local da integração em 19/11/2026

Branch `integracao/rede-voz`, base `a4a693e`, com os ajustes locais de voz. Os processos usaram o mesmo computador.

| Cenário | Resultado observado |
|---|---|
| Descoberta e eco UDP | O cliente encontrou `Birds D1` em `127.0.0.1:42917` e recebeu os ecos 1, 2 e 3 em ordem. O host detectou a queda após o encerramento forçado do cliente. |
| Três e quatro participantes | Um host de teste usando `UdpSession` atendeu primeiro dois e depois três clientes simultâneos. Cada cliente recebeu os três ecos em ordem. Essa verificação cobre a camada de rede local. |
| Partida com duas instâncias de `./birds --rede-teste` | O host registrou entrada, coleta de dois cajus, entrega dos dois e saída. O cliente viu o host, recebeu nove NPCs e confirmou a entrega. Ambos encerraram com código zero. |
| Voz | As 67 verificações de voz em memória passaram na versão com o ajuste de ganho e tom. |

Para fechar a validação em campo, falta registrar a descoberta e os ecos no computador cliente, repetir a queda desligando o Wi-Fi e validar o grupo no hotspot da apresentação. Na partida com microfone e fones, conferir se as palavras continuam inteligíveis e sem saturação durante a aproximação e o afastamento.
