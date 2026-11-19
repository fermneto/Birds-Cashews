# Resultados do teste de rede da D1

Registro do teste feito em 18/11/2026 com a branch `daniel/d1-rede`, no commit `f98ae6d`, que depois entrou na `main`
pelo PR #6. O roteiro está em [`TESTE_REDE.md`](TESTE_REDE.md).

**Situação: parcialmente aprovado.** As mensagens do cliente chegaram ao host entre dois computadores, e o host detectou
a saída do cliente. Ainda faltam as confirmações listadas na seção 5.

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

`runNetworkSelfTest` foi executado à parte em 18/11/2026, com a `main` no commit `8976ab6`, porque ainda não faz parte de
`./birds --selftest`. Todas as verificações passaram:

| Verificação | Resultado |
|---|---|
| Descoberta local da sala | Aprovada |
| Entrada por IP usando a porta padrão | Aprovada |
| Host atribui o ID do jogador | Aprovada |
| Mensagem entra na fila de envio | Aprovada |
| Mensagem e confirmação perdidas são reenviadas, sem duplicar e em ordem | Aprovada |
| Queda detectada sem pacote de saída | Aprovada |
| Host remove a sessão expirada | Aprovada |

## 5. Pendências

1. Daniel confirmar se o `discover` mostrou `Birds D1` e se apareceram `Eco recebido 1`, `2` e `3`.
2. Repetir a queda desligando o Wi-Fi do cliente.
3. Repetir o roteiro completo no hotspot do celular, que é a rede da apresentação.
4. Testar 3 e 4 jogadores conectados, porque o teste usou apenas um cliente.
5. Registrar `runNetworkSelfTest` em `./birds --selftest`, para que esses testes rodem a cada alteração.
