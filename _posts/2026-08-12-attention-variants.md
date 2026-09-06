---
layout: post
title: "不同的注意力"
date: 2026-08-12 20:03:00 +0800
tag: 人工智能
---

## MHA，multi-head attention

MHA的核心本质是将注意力拆分成多个并行的头，每个头独立学习特征之间的关联关系（学习不同维度的特征表示，比如学习主语特征的，学习谓语特征的等等），并将注意力分数结果拼接起来，在保证总的维度不变的情况下，大幅提升模型对特征的捕捉能力和表达上限。

![不同的注意力配图 1]({{ '/assets/images/2026/attention-variants/01.png' | relative_url }})

## MQA，multi-query attention

MQA是19年提出的，背景是在MHA中应用KV cache后导致显存占用提升，模型在计算的时候需要从显存将缓存的KV移动到计算单元中，这个时候模型的瓶颈就不是算力而是显卡的带宽能力了。如果KV cache太大，就导致计算减慢，显存带宽的瓶颈是因为KV cache太大，那就减小KV cache，于是有了MQA。

![不同的注意力配图 2]({{ '/assets/images/2026/attention-variants/02.png' | relative_url }})

在MHA中，每一个QKV头一一对应，但是在MQA中，保持Q头的数量不变，但是KV均只有一个头，所有的Q头都共用这一个KV头，计算注意力时，直接copy多份KV头去和Q头计算。

![不同的注意力配图 3]({{ '/assets/images/2026/attention-variants/03.png' | relative_url }})

## GQA，grouped-query attention

将查询头划分成多个组，每个组内共用一对相同的KV头。GQA分组数为1，变成MQA，分组数等于Q头的数量，变成MHA。相比于MQA表达能力有了提升，同时也减小了KV 缓存大小。

![不同的注意力配图 4]({{ '/assets/images/2026/attention-variants/04.png' | relative_url }})

## 显存比较

![不同的注意力配图 5]({{ '/assets/images/2026/attention-variants/05.png' | relative_url }})

## Sparse Attention

牺牲了一些精确度，达到超长的上下文窗口。随着N越来越大，KV cache的显存占用也越来越大。

![不同的注意力配图 6]({{ '/assets/images/2026/attention-variants/06.png' | relative_url }})

第一步，随机的选样计算注意力，第二步，沿着对角线附近计算注意力，第三步计算global tokens，最终汇集前面的得到结果注意力矩阵。

![不同的注意力配图 7]({{ '/assets/images/2026/attention-variants/07.png' | relative_url }})

![不同的注意力配图 8]({{ '/assets/images/2026/attention-variants/08.png' | relative_url }})

形象表示

![不同的注意力配图 9]({{ '/assets/images/2026/attention-variants/09.png' | relative_url }})


![不同的注意力配图 10]({{ '/assets/images/2026/attention-variants/10.png' | relative_url }})

all-together，超长上下文技术总结。

![不同的注意力配图 11]({{ '/assets/images/2026/attention-variants/11.png' | relative_url }})

## 参考资料

[MHA、MQA、GQA](https://www.bilibili.com/video/BV1xVwezbEZj/?spm_id_from=333.1387.collection.video_card.click&vd_source=276daa9207bfc3366d4730ad90187a50)
