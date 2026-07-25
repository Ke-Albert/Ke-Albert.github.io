---
layout: post
title:  "transformer"
date:   2025-11-21 21:43:31 +0800
# categories: deep learning
tag: 人工智能
---
# 第一种方式
transformer的输入输出不改变维度大小。q,k,v单独构建，单独计算。
```python
def forward(self,q,k,v,mask=None):
        batch,time,dimension=q.shape#128,32,512,
        n_d=self.emb_dim//self.n_head# 512//8==64
        q,k,v=self.w_q(q),self.w_k(k),self.w_v(v)
        q=q.view(batch,time,self.n_head,n_d).permute(0,2,1,3)#128,8,32,64
        k=k.view(batch,time,self.n_head,n_d).permute(0,2,1,3)
        v=v.view(batch,time,self.n_head,n_d).permute(0,2,1,3)
        attn=q@k.transpose(-2,-1)/math.sqrt(n_d)#128,8,32,32
        if mask is not None:
            mask=mask.unsqueeze(1).unsqueeze(2)#1,1,32,32
            attn=attn.masked_fill(mask[:,:,:time,:time]==0,float('-inf'))
        attn=self.softmax(attn)@v#128,8,32,64
        attn=attn.permute(0,2,1,3).contiguous().view(batch,time,dimension)
        out=self.w_combine(attn)
        return out
```
# 第二种方式
q,k,v一起都在一个大的矩阵中，一起计算。
```python
def forward(self,x,mask=None):
        B,T,C=x.shape
        qkv=self.qkv(x)
        qkv=qkv.reshape(B,T,3,self.num_heads,self.head_dim)
        qkv=qkv.permute(2,0,3,1,4)#(3,B,num_heads,T,head_dim)
        q,k,v=qkv.unbind(0)#(B,num_heads,T,head_dim)
        attn=(q@k.transpose(-2,-1))*self.scale#(B,num_heads,T,T)
        if mask is not None:
            mask=mask.unsqueeze(1).unsqueeze(2)
            attn=attn.masked_fill(mask[:,:,:T,:T]==0,float('-inf'))
        attn=self.softmax(attn)
        out=(attn@v)#(B,num_heads,T,head_dim)
        out=out.transpose(1,2)#(B,T,num_heads,head_dim)
        out=out.reshape(B,T,-1)#(B,T,C)
        out=self.proj(out)
        return out
```
# 第三种方式
q,k,v单独构建，单独计算。但是变化维度的矩阵分为了正向和反向变化，用来改变q,k,v的维度。
```python
def forward(self,x,mask=None):
        # queries，keys，values的形状:
        # (batch_size，查询或者“键－值”对的个数，num_hiddens)
        # valid_lens　的形状:
        # (batch_size，)或(batch_size，查询的个数)
        # 经过变换后，输出的queries，keys，values　的形状:
        # (batch_size*num_heads，查询或者“键－值”对的个数，
        # num_hiddens/num_heads)
        queries=transpose_qkv(self.q(x),self.num_heads)
        keys=transpose_qkv(self.k(x),self.num_heads)
        values=transpose_qkv(self.v(x),self.num_heads)

        attn=queries@keys.transpose(-2,-1)
        if mask is not None:
            mask=mask.unsqueeze(1)
            attn=attn.masked_fill(mask[:,:queries.shape[-1],:queries.shape[-1]]==0,float('-inf'))
        attn=self.softmax(attn)
        y=attn@values
        y=transpose_out(y,self.num_heads)
        y=self.o(y)
        return y
```
# 第四种方式
几乎和第二种一模一样。
```python
def forward(self,x,mask=None):
        B,T,C=x.shape # B:batch_size,T:seq_len,C:embed_dim
        qkv=self.qkv_layer(x)#B,T,3*embed_dim
        q,k,v=qkv.split(self.embed_dim,dim=-1)#B,T,embed_dim
        q=q.view(B,T,self.num_heads,self.head_dim).transpose(1,2)#B,num_heads,T,head_dim
        k=k.view(B,T,self.num_heads,self.head_dim).transpose(1,2)#B,num_heads,T,head_dim
        v=v.view(B,T,self.num_heads,self.head_dim).transpose(1,2)#B,num_heads,T,head_dim
        att=q@k.transpose(-2,-1)*(1/math.sqrt(k.shape[-1]))#B,num_heads,T,T
        if mask is not None:
            mask=mask.unsqueeze(1).unsqueeze(2)
            att=att.masked_fill(mask[:,:,:T,:T]==0,float('-inf'))
        att=self.softmax(att)
        y=att@v#B,num_heads,T,head_dim
        y=y.transpose(1,2).contiguous().view(B,T,C)
        y=self.proj_layer(y)
        return y
```

# Vision Transformer

