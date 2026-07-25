---
layout: post
title:  "zero-to-hero"
date:   2025-10-19 15:43:31 +0800
tag: 人工智能
---

<div class="post-toc" id="post-toc">
  <div class="post-toc-title">目录</div>
</div>

这篇文章记录了从手动实现自动微分到构建 GPT 训练逻辑的完整学习路径，重点包括微分、二元模型、N-gram、BatchNorm、Transformer 与 Tokenization 的核心思想与代码实现。

# Part 1-Micrograd
要自动计算梯度，需要一个类来实现，这个类能够表示标量和张量，需要计算变量的梯度时，能够调用这个类（实例）的相关计算梯度的方法。可以简单地创建一个Value类:
```python
class Value:
    def __init__(self, data, _children=(), _op=''):
        self.data = data
        self.grad = 0
        self._backward = lambda: None
        self._prev = set(_children)
        self._op = _op
```

这样，梯度和求导函数都包括在了这个类里面，还记录了它依赖于哪些变量即由哪些变量和操作计算得来。以tanh为例：
```python
class Value:

    ...

    def tanh(self):
        x = self.data
        t = (math.exp(2*x) - 1)/(math.exp(2*x) + 1)
        out = Value(t, (self, ), 'tanh')
        def _backward():
            self.grad += (1 - t**2) * out.grad
        out._backward = _backward
    return out
```
在这里，out为计算的tanh值，它是一个Value对象并最终返回out，例如a=Value(1),b=a.tanh()，在计算tanh时，也会创建一个`_backward()`函数，它被赋值给了新创建的out变量，它记录了当前变量需要计算梯度时的公式，即这样的逻辑`还是以a,b为例，当需要从b反向传播计算a的梯度时，将b的梯度设为1，调用b的反向传播函数，它是用以计算a的梯度。反向传播函数不是同级对等的关系，即a的梯度需要调用b的反向传播函数计算得到，而不是a的反向传播函数，如果a的反向传播函数不为None，那么它则是计算在a之前的变量的梯度，而不是a本身的梯度，以y=x**2为例，要计算x关于y的导数，是通过y对x求导，而不是对x本身求导，y对其自身的导数为1`，这样当需要调用反向传播函数时，就可以计算得到当前变量的梯度值，这里的梯度是`+=`而不是直接赋值，这是因为同一变量可能会在多个不同的地方使用到，正确的计算方式就是将这些不同地方的但是是同一变量的梯度进行累加。这也解释了在`PyTorch`中，每一轮训练开始在进行反向传播之前需要将变量的梯度清0,为的就是不让上一轮的梯度继续与本轮的梯度累积。

当需要进行反向传播时，我们需要一个搜索算法来将所有的与最终作为反向传播开始的变量相关的变量都找出来，例如a=Value(1),b=a.tanh(),c=b**2，当需要从c反向传播计算a的梯度时，需要先将c的梯度设为1，然后调用c的反向传播函数，它会计算b的梯度，而a的梯度需要调用b的反向传播函数计算得到，所以需要一个搜索算法来将所有的与c相关的变量都找出来，即a,b,c。
```python
class Value:
     ...
     def backward(self):
        topo=[]
        visited=set()
        def build_topo(v):
            if v not in visited:
                visited.append(v)
                for child in v._prev:
                    build_topo(child)
                topo.append(v)
        build_topo(self)
        self.grad=1.0
        for v in reversed(topo):
            v._backward()
```
这是一个拓扑排序的过程，它将所有的与最终作为反向传播开始的变量相关的变量都找出来，然后从后往前调用每个变量的反向传播函数，这样就可以计算得到所有变量的梯度值。使用的是深度优先算法，这里用了两个变量来记录拓扑排序和已访问变量节点，理论上来说可以只使用`topo`一个变量，这里用了`visited|set`是因为集合查找比列表查找更快，列表的查找时间复杂度是O(log(n)),而集合则是O(1)。
有了能够进行自动微分反向传播的类，我们就可以按照Neuron->Layer->MLP的顺序构建一个简单的多层感知机，并进行训练。它不支持矩阵并行计算这样的复杂操作，底层实现逻辑还是通过循环来获取单一的元素进行运算的。

# Part 2-Bigram
从统计模型角度来看二元语法模型，字符级别的预测例如句子`我喜欢你`,我们根据单个字符进行预测，首先在句子的首位添加起止符，比如可以使用`.我喜欢你.`这样的表达，`.`表示句子的开始和结束，起止符号可以相同，也可以采用不同的表示，这里采样相同的表示。将字符转换成数字显然更加符合计算机的习惯，也是更方便计算。所以对于'abc...z'这样的字符，再加上`.`这样的起止符，在这个简单的模型中一共有27个独特的字符，设计两个字典方便从字符到数字之间的互相转换。可以统计在使用的数据集中，二元字符对出现的频率，这应该是一个[27,27]的二维矩阵，因为每个字符都可以作为二元模型中的第一个字符，也可以作为二元模型中的第二个字符，总之，我们可以经过统计得到这样的以每个字符开头的二元对的频率，并将它友好地显示出来。从可视化中可以明显看出，一些字符对的出现频率很高，一些则一次都没有，这即揭示了一些统计规律（也可能是当前数据集不够具有代表性），但不管怎样，我们可以根据当前的数据集得到符合当前数据集的统计规律，比如`wq`字符对在当前数据集中出现的频率为0。接着，我们可以独立地对每一行求和，并计算各自的概率即softmax运算。这样就得到了基于当前字符作为第一个字符，其二元模型中第二个字符出现的各个近似概率（总之如果数据集足够具有代表性，也即数据集足够大，大数定律告诉我们当然可以近似地认为这就是这个字符对的概率）。这就是简单的二元语法的统计模型，接下来我们就可以进行采样了，生成一系列`预测`的合理的人名。

简单的统计模型，换个角度我们也可以使用神经网络的方式来实现，因为当到达三元、四元、n元模型时，简单的统计模型就显得捉襟见肘了，单单想一想各种组合的可能性，就是呈指数级的爆炸增长。而神经网络可以在简单和复杂中都能有效地进行表示（建模）和计算。在概率与数理统计中，我们学习过最大似然估计，简而言之，最大似然估计构建了一个这样的模型，这个模型符合使得当前数据集中n元模型出现的概率达到最大--即利用已知的样本结果信息，反推最具有可能（最大概率）导致这些样本结果出现的模型参数值，模型的复杂问题一切都包含在了什么模型最能导致当前的样本结果了。`∏𝑖=1𝑛𝑝𝜃^(𝑥𝑖)，这就是最大似然函数。对于连续型随机变量，有相同的结论。`。深度学习中我们一般都是最小化损失，换个思路最大化似然函数->最小化负的最大似然函数->最小化数据集中的所有数据的平均负的最大似然函数，按照惯例为了计算方便，我们将其对数化，这样乘法就变成了加法`log(a*b*c)=loga+logb+logc`，于是这样的逻辑思路就是`利用最大似然函数的思想我们构建了一个对模型损失的评估，通过反向传播算法我们不断根据当前的损失去更新模型的参数。`不过当遇到从没有出现过的字符对时，这个log值就为`-inf`，可以给整体各自加上一个较小的值，比如1，这样最小频率为1，这个方式称为模型平滑。为了训练神经网络，需要构建训练样本标签对，例如`.emma.`这样的单词，划分成训练样本对为`xtr=[.,e,m,m]`和`ytr=[e,m,m,a,.]`，在输入网络前还需要将其转换成数字的形式`[0,5,13,13,1]`和`[5,13,13,1,0]`。转换成数字的形式之后，可以看到时间序列xtr中当前的词去预测下一个词，在下一个时间步中被预测的下一个词又作为当前的词继续去预测它的下一个词，因为要使用神经网络，y=X@W+b这样的形式，可以把xtr-ytr变为二维即`shape=[time_step,1]`的形式，但是这样矩阵相乘时，就要求W的形状是`shape=[1,hidden_representation]`的大小，即`[[0],[5],[13],[13],[1]]@[[n1,...,n_h]]`，简单的矩阵乘法知识就可以知道，它的结果仅仅是`C=A@B`中，C的每一行的结果都是A的每一行乘以B的每一列。而使用`one_hot`编码，将`x_tr-y_tr`根据其字典大小的长度进行编码，可以让`x_tr-y_tr`的形状变成`shape=[time_step,27]`，这样W的形状就可以是`shape=[27,hidden_representation]`，可以进行更复杂的线性组合，获得更复杂的表示，独热码只是encodeing的其中一种最简单的方式，本质就是给当前的字符一个在高维空间上的表示方法，还可以通过一些词袋模型计算更复杂的编码表示方式比如`word2world`。在这里，简单将隐藏表示维度设为27，那么`logits=xenc@W`的结果被赋予了`logits`的称呼，这也即与统计模型中的出现次数对应一个级别，但是这里的`次数`有正有负，因为W是随机初始化的。对`counts=logits.exp()`将其全部转为正数，然后计算其softmax结果，就得到了统计模型中每一行相同的频率（概率）表示形式，有了概率表示就可以计算最大似然函数，在未训练模型时，我们还可以估计平均最大似然函数正常的取值范围，因为在没有训练之前，没有任何理由可以认为什么组合的出现概率最高，所以它们出现的概率是相等的，平等看待每一个组合，这样就计算出一个最大似然函数的值，可以用来评估初始化权重矩阵是否合理。从负的最大似然函数开始进行反向传播，使用梯度下降算法训练网络。最后同样地可以得到与统计模型相似地结果。

W权重矩阵和二元模型概率分布及其正则化。W的形状是`shape=[27,27]`，而我们使用`one_hot`编码，根据矩阵相乘的结果，每一行即每一个时间步的输入，都被映射到了一个27维的向量空间中，这个向量空间中的每个维度，都对应了一个字符，而这个字符的出现概率，就是这个向量空间中这个维度的数值。所以W的每一行，就是对应了一个字符作为第一个字符，其二元模型中第二个字符出现的概率分布。通过矩阵相乘获取了与统计模型相同的表示结果。而在统计模型中为了避免log取值出现负无穷，我们通过为最小值增加一点值作为模型平滑的结果，这个量加得越大，最终计算的概率越平滑，各个组合出现的概率越相近，同样地在W权重矩阵中，如果W的各个元素其初始值都被设置为0，那么取得的结果就是一个均值，各个组合的概率相同。这就引出了正则化，通过L2正则化，我们在实现梯度下降更新W参数的同时，也在尽可能让W变小，我们可以实现相同的平滑效果，这就是正则化和模型平滑的一个共通解释，很有趣。

# Part 3-N-gram模型
现在，可以利用神经网络模型，将二元模型扩展到N元模型，以N=3为例。可以构建训练验证测试数据集。现在我们设置embedding的大小为10,即`C.shape=[27,10]`,它代表了每个字符被映射到了一个10维的向量空间中。通过索引`C[X]`,可以获取到输入序列X中每个时间步下每个字符对应的10维向量表示，而`C[X].shape=[N,3,10]`，接下来就是相似地构建`W1.shape=[30,200],W2.shape=[200,27]`权重矩阵,同样地训练模型。在计算损失函数时，我们的操作一般是将取得的logits结果进行取指数(e)运算，全部变为正值，再使用softmax函数计算各自的概率，最后取log值求平均计算最大似然函数，现在，这些操作全部可以简化为`F.cross_entropy(logits,Y)`这一个表达式中。这就是N元模型，相比于二元模型没有任何特别的，都可以使用神经网络模型一步一步构建得到,但是一些地方也有变化，比如网络深度，隐藏层数，网络宽度，embedding大小，学习率变化，这些都是构建网络过程中的超参数。
此外，无论如何构建多么复杂的网络，网络的损失都不会变为0，一个较为直观的解释是，每个N元模型的开始都是以`<S>`作为第一个字符,所以通过起止符去预测下一个元素时，我们需要这种变化，如果每一次通过起止符去预测下一个元素的值都是不变的，这本来就自相矛盾。比如`<S>你好<E>`，`<S>我喜欢你<E>`。

# Part 4-BatchNorm
在早期的深度学习实践中，对于权重矩阵的初始化需要十分精准，避免在张量传播过程中，其值域范围的均值和方差出现极端的不稳定情况。
```python
g=torch.Generator().manual_seed(2147483647)
C=torch.randn((vocab_size,n_embd),generator=g)
W1=torch.randn((n_embd*block_size,n_hidden),generator=g)*(5/3)/((n_embd*block_size)**0.5) #*0.2
b1=torch.randn(n_hidden,generator=g)*0.01
W2=torch.randn((n_hidden,vocab_size),generator=g)*0.01
b2=torch.randn(vocab_size,generator=g)*0.01
```
在该代码示例中，假如初始化时没有乘以系数，那么在第一次前向传播过程中，各个层的所得的均值和方差就会出现极端的不稳定情况，这会导致梯度消失或梯度爆炸的问题，从而影响模型的训练效果。以W1为例，`h=tanh(X@W1+b1)`,tanh函数的图像如下所示，它也是属于sigmoid函数簇
![tahn](/assets/images/2025/tanh.png)
它的导数为`1-h**2`,链式规则为`self.grad=(1-h**2)*out.grad`从公式上可以看出，当h接近-1或1时，导数接近0，这会导致梯度消失的问题。而当h接近0时，导数接近1，它会传递梯度值。而如果没有合理的初始化W1权重矩阵，在第一次前向传播过程中，h的取值会非常大或非常小，这会导致梯度消失或梯度爆炸的问题。所以，在初始化W1权重矩阵时，需要乘以一个系数，比如`(5/3)/((n_embd*block_size)**0.5)`，这是根据tanh函数的性质推导出来的一个系数，它可以确保在第一次前向传播过程中，h的均值和方差不会出现较大的波动，从而避免梯度消失的问题。
当然我们可以凭借直觉和反复实现观察其内部的数值，来给定一个较为还不错的系数。而系统性的初始化方法，比如Xavier初始化，kaiming初始化，则在工程上系统性地对初始化进行了优化，避免了手动给定系数的过程，同时也确保了模型的训练效果。
当然这只是网络第一次训练中的第一个batch过程，为训练开了一个比较好的头。为了在整个训练过程中都保持较好的稳定状态，提出了一系列的归一化方法。比如batch normalization，layer normalization，instance normalization等。这些方法的基本思想都是在每个batch或每个样本中，对输入的特征进行归一化，从而避免梯度消失或梯度爆炸的问题。我们可以这样想象，有一个X=[x1,x2,...,xn]它有n个特征，其中每个特征的数值尺度是不一样的，范围从1-10000变化，那么进行梯度下降时，它们的更新速度或者说走的step的尺度也是不一样的，有的走得快，有的走得很慢。比较好的处理方法就是把这些特征的尺度都进行归一化处理，让它们都在同一个尺度下面进行训练。这就是归一化。
怎么在代码中手动实现一个batch normalization呢？原理很简单，我们可以在每一个batch中，计算当前batch的均值和方差，再对当前batch中的每个样本，减去均值后再除以方差，从而实现归一化。`hpreact=bngain*(hpreact-hpreact.mean(0,keepdim=True))/hpreact.std(0,keepdim=True)+bnbias`，为了让网络能够调整分布，使得一些神经元激活一些不敏感，引入bngain/gamma和beta/bnbias两个可学习的参数，这是因为我们不希望一直让网络强制保持标准的高斯分布，只是在前期没有任何知识的情况下无法假设，只能保持公平性，不让网络对任何一个对象有偏爱，但当随着网络训练的过程，网络能够偏爱一些胜过另一些，也就是神经元敏感与不敏感。
现在我们引入了bn，但这也引入了一个问题，我们的训练过程与数据出现了耦合。隐藏状态、激活值除了依赖于输入X、函数，还依赖随机选取的batch形成的组合数据，比如一些batch中的样本的某个特征的取值范围很大，而另一些样本的取值范围很小，这种随机抖动的作用，反而可以作为一种正则化，引入一点熵让模型难以过拟合。
不过，这也引入了一个问题，就是在预测时，如何在模型中已有的计算批量状态下的bn中进行适配？预测时我们输入的是一个样本，而不是一个batch，但是现在有一段代码是计算bn的。这里有两种实现方式，一种是固定训练集中的bn值，当训练完成后，重新计算整个训练集的bn值，当预测时使用这个固定的bn值。另一种实现方式则是采用动态更新的策略，动态评估bn值。省略了重新计算整个训练集的步骤。它的实现方式如下,基本思想就是记录一个running的状态，然后根据是否是训练过程来决定要不要计算当前批次的均值和方差，如果不是训练过程，则直接将记录的running状态的均值和方差赋值给实际要使用的均值和方差变量，再进行归一化处理。并且，如果是训练过程，就会在最后根据momentum这个动量的大小决定要更新到running状态上的分量，一般而言momentum取0.1代表取当前计算得到的批次的均值和方差的0.1，将其加入到running状态值中，这样就实现了根据每一个批次的均值和方差，对最终的均值和方差的更新。
```python
class BatchNormld:
  def __init__(self,dim,eps=1e-5,momentum=0.1):
    self.eps=eps
    self.momentum=momentum
    self.training=True
    #parameters trained with backprop
    self.gamma=torch.ones(dim)
    self.beta=torch.zeros(dim)
    #buffers (trained with a running 'momentum update')
    self.running_mean=torch.zeros(dim)
    self.running_var=torch.ones(dim)

  def __call__(self,x):
    #calculate the forward pass
    if self.training:
      xmean=x.mean(0,keepdim=True)
      xvar=x.var(0,keepdim=True)
    else:
      xmean=self.running_mean
      xvar=self.running_var
    xhat=(x-xmean)/torch.sqrt(xvar+self.eps)
    self.out=self.gamma*xhat+self.beta
    #update the buffers
    if self.training:
      with torch.no_grad():
        self.running_mean=(1-self.momentum)*self.running_mean+self.momentum*xmean
        self.running_var=(1-self.momentum)*self.running_var+self.momentum*xvar
    return self.out

  def parameters(self):
    return [self.gamma,self.beta]
```
# Part 5-Backprob
针对简单的多层感知机，其梯度反向传播较为简单，在计算时需要从矩阵的角度考察衡量，要注意是否需要沿着某一个维度/轴进行求和，因为在前向传播过程中会隐含地出现广播这个操作，所以很容易忽略掉。如果原来的变量是矩阵形式，那么其反向传播时的梯度也是矩阵形式。简而言之，变量正向和反向传播的形状都是不变的。

# Part 6-Building a WaveNet
这里面基本是按照第三部分的内容，不过将一些混乱的结构进行了整理，将Embedding的过程单独整理成了一个类。添加了展平层，此外对bn层进行了扩展，支持不同的维度。

# Part 7-Let's build GPT: from scratch
通过一个简单的二元模型，该二元模型做的仅仅是将输入的token的整数索引映射到一个向量中，该向量的大小也是token的词典大小，表示的是从该token映射到下一个可能token的Logits值。它的参数大小是[vocab_size,vocab_size]，`logits=self.token_embedding_table(idx)`，举例该例子主要是为了实现一个简单的生成器，根据当前的输入生成下一个预测的输出。可以看到，我们只需要拿取计算的最后一个时间步的logits，然后通过softmax计算得到概率，根据该概率采样得到下一个预测的词的idx_next，然后将这个id_next添加到idx中，作为输入，在这个例子中由于是二元模型，所以只会用到前后两个词，但是作为通用的生成器，这里可以简单地修改，就可以利用规定的上下文大小比如8个词的上下文，去预测下一个词,比如我们可以更改block_size的大小，就可以只关注于最新的需要查看的上下文大小的词，使用这些最新的词去预测下一个词，而计算注意力这些额外的计算，都放在了forward函数中，如果我们注释掉这一行，那么并且现在forward函数中没有实施注意力相关的计算，就退回了最初的二元模型。此时在没有实施注意力代码的时候，你会怎么考虑控制上下文大小的注意力计算呢？我最初的想法很直接，注意力计算要考虑上下文大小，那就直接在注意力计算的过程中进行控制，但是这样又引入了新的问题，就是控制所选上下文的窗口的移动，比如现在的上下文窗口大小是8，现在我的输入有32个，那就要控制找到新的8个词的内容，这就会要求我们额外地在forward函数中添加额外的控制。但是我们不想因为这个改变通用的写法，所以换个思路，把这个窗口大小的控制逻辑放在了生成器中，直接截取idx的最新的上下文窗口大小的内容，这样，在forward函数中只需要按照输入计算注意力就可以了。
```python 
def forward(self, idx, targets=None):

        # idx and targets are both (B,T) tensor of integers
        logits = self.token_embedding_table(idx) # (B,T,C)

        if targets is None:
            loss = None
        else:
            B, T, C = logits.shape
            logits = logits.view(B*T, C)
            targets = targets.view(B*T)
            loss = F.cross_entropy(logits, targets)

        return logits, loss

def generate(self, idx, max_new_tokens):
    # idx is (B, T) array of indices in the current context
    for _ in range(max_new_tokens):
        # get the predictions
        idx_cond=idx[:,-block_size:]
        logits, loss = self(idx_cond)
        # focus only on the last time step
        logits = logits[:, -1, :] # becomes (B, C)
        # apply softmax to get probabilities
        probs = F.softmax(logits, dim=-1) # (B, C)
        # sample from the distribution
        idx_next = torch.multinomial(probs, num_samples=1) # (B, 1)
        # append sampled index to the running sequence
        idx = torch.cat((idx, idx_next), dim=1) # (B, T+1)
    return idx
```
注意力的数学本质，就是求得加权后的新值，理解的意思就是W是权重矩阵，而X是待加权的变量，通过W@X，就可以根据W中的权重来重新调整X中每一个元素的值，该值参考了其它值，在该元素对应位置生成的新的加权后的元素值，其是由与该位置有关的原来的元素值，以及其它相关值通过加权运算得到的。
```python
torch.manual_seed(1337)
B,T,C = 4,8,32 # batch, time, channels
x = torch.randn(B,T,C)

# let's see a single Head perform self-attention
head_size = 16
key = nn.Linear(C, head_size, bias=False)
query = nn.Linear(C, head_size, bias=False)
value = nn.Linear(C, head_size, bias=False)
k = key(x)   # (B, T, 16)
q = query(x) # (B, T, 16)
wei =  q @ k.transpose(-2, -1) # (B, T, 16) @ (B, 16, T) ---> (B, T, T)

tril = torch.tril(torch.ones(T, T))
#wei = torch.zeros((T,T))
wei = wei.masked_fill(tril == 0, float('-inf'))
wei = F.softmax(wei, dim=-1)

v = value(x)
out = wei @ v
```
在这个例子中，key和query都是从原x中通过矩阵投影而来，通过这两个投影计算得到wei即权重矩阵，同样地我们也再一次对x投影得到v值，那么v的加权后的新值，就是`out=wei@v`，这就是注意力的计算过程。不过此时的形状还是[B,T,head_size]，还要使用一个投影矩阵将其投影回原来的大小[B,T,C]，我们需要记住的是，经过注意力加权后，新的变量的形状应该与原来的变量形状保持一致，因为我们仅仅是做一个加权处理，如果最终改变了形状就不正确了。同样地需要注意，在生成式语言模型中，就是根据上下文去预测下一个词时，按照正常逻辑，我们应该只能看到截止到当前时间步及其以前时间步的内容，计算它们之间的注意力值，这里我们使用掩膜将上三角的值换成了`-inf`,这就符合了[生成]这个词所表达的含义。
需要注意的是，
1.  在计算注意力时没有空间相对位置的关系，想想`a=a_1*w_1+a_2*w_2+a_3*w_3`等同于`a=a_3*w_3+a_1*w_1+a_2*w_2`，所以我们需要自己额外添加一个位置编码进去。
2.  注意力可以看作一种交流机制，可以被视为有向图中的节点，这些节点相互观察，并通过来自所有指向它们的节点的加权和来聚合信息，其中权重取决于数据。
3.  在batch维度跨越的样本之间是无法交流的，因为矩阵乘法只发生在最后两个维度。
4.  在编码器中，只需要删除进行掩膜的那行代码，就从解码器变成了编码器，从历史的原因分析，最初transformer架构的提出是针对语言翻译的，所以使用编码器去了解翻译对象的全部信息，再使用解码器也就是对注意力权重矩阵增加了掩膜的部分，去生成翻译的目标语言。单独的解码器架构也就是自回归模型，常用于大语言模型。
5.  自注意力，就是说除了query是来自于x外，key,value也都来自于x的投影变换，如果query，value来自于其它数据，就叫做cross-attention。
6.  “缩放”注意力机制额外将wei除以1/√(头大小)。这样一来，当输入Q、K具有单位方差时，wei也会具有单位方差，并且Softmax会保持分散状态，不会过度饱和。

# Part 8-Tokenization
LLM的许多奇怪的现象都和tokenization有关。例如，大语言模型不能很好的拼写单词，在编写代码方面表现很差，这些都与tokenization有关。
在本例中针对字符级别，将其转为utf-8编码，然后转为整数。
```python
print('你好'.encode('utf-8'))
b'\xe4\xbd\xa0\xe5\xa5\xbd'
print(list(map(int,'你好'.encode('utf-8'))))
[228, 189, 160, 229, 165, 189]
```
这样我们便得到了用整数表示的一些列token。仅仅一个`你好`的token表示就是6个整数，每个整数都在0到255之间。而具有这样规律的整数在大量文本中会重复出现，这就是语义的表达。而大预言模型只是接收这些整数作为输入，然后根据其内部的参数，进行预测下一个token出现的概率。我们可以将[228, 189, 160, 229, 165, 189]这样的具有语义信息的整数进行融合，创造出一个新的整数来代表该序列，就是使用256来代表`你好`，重复这样的操作我们就可以得到扩充了的，压缩了信息的token库。但是token库的大小也不能无限大，例如使用10000这个整数来表达`针对简单的多层感知机，其梯度反向传播较为简单`这样一句话，已经压缩了许多信息，无法获取其内部的详细语义信息，所以token库的大小要适合就行，也就对应了我们需要融合的次数，每次我们融合都选择出现频率最高的一对整数，并将其赋予新的值，只要我们在合适的合并次数下停止，就能够得到较为合理的，既能合理高效表示语义信息，又不至于压缩得太厉害的程度。
```python
def get_stats(ids):
    counts = {}
    for pair in zip(ids, ids[1:]): # Pythonic way to iterate consecutive elements
        counts[pair] = counts.get(pair, 0) + 1
    return counts
```
我们使用`get_stats`函数，对输入得tokens进行统计，得到所有相邻的token对出现的次数并返回。
```python
def merge(ids, pair, idx):
  # in the list of ints (ids), replace all consecutive occurences of pair with the new token idx
  newids = []
  i = 0
  while i < len(ids):
    # if we are not at the very last position AND the pair matches, replace it
    if i < len(ids) - 1 and ids[i] == pair[0] and ids[i+1] == pair[1]:
      newids.append(idx)
      i += 2
    else:
      newids.append(ids[i])
      i += 1
  return newids
  ```
然后我们可以使用`merge`函数，融合指定的token pair，并赋值为新的token idx，返回的是融合后的token序列，原来的token pair被token idx替代了。所以，如果原token长度为n，其中有m个token pair，那么融合后token序列的长度为n-m。下面这段代码将整个融合的流程串联了起来，首先获取token pair对，然后对获取的token pair对进行排序，获得出现频率最高的pair，之后为这个pair赋值一个新的token整数值，来代替原来的pair，实现信息的高效、压缩表示，并用一个字典记录被替换的pair和它的新的整数token值的表示之间的映射关系，方便后续解码的时候反推回原来的表示。
```python
vocab_size = 276 # the desired final vocabulary size
num_merges = vocab_size - 256
ids = list(tokens) # copy so we don't destroy the original list

merges = {} # (int, int) -> int
for i in range(num_merges):
  stats = get_stats(ids)
  pair = max(stats, key=stats.get)
  idx = 256 + i
  print(f"merging {pair} into a new token {idx}")
  ids = merge(ids, pair, idx)
  merges[pair] = idx
```
我们通过这个循环，融合了20个token pair，得到了一个新的token库，其中包含了256个原始token，以及20个新的token，每个新的token都是由两个原始token融合而来的。并且可以比较融合后的token长度和原始token长度得到一个压缩率，用来表示压缩的程度。Tokenizer是一个独立于LLM的模块，有其自己的训练集，使用上面的方法即Byte pair encoding(BPE)算法，它将在文本和一些列的tokens之间转换，输入到LLM中的只有转换后的tokens。
![tokenizer](/assets/images/2025/tokenizer.png)
## Decoding
现在我们已经实现了融合token pair的操作，就是我们已经根据训练集训练好了一个tokenizer，现在我们需要应用到实际中。但是怎么解码呢？即怎么从一段tokens中，恢复出原来的文本？很简单的直观的方法就是根据融合的链式替换，一步步将新的token idx替换为原来的token pair，直到恢复出原来的文本。不过使用字节级的形式，可以很取巧地快速解码，因为字节的表示可以相加，就像字符一样进行拼接。`bytes([66])+bytes([67])==b'BC'`
```python
vocab = {idx: bytes([idx]) for idx in range(256)}
for (p0, p1), idx in merges.items():
    vocab[idx] = vocab[p0] + vocab[p1]

def decode(ids):
  # given ids (list of integers), return Python string
  tokens = b"".join(vocab[idx] for idx in ids)
  text = tokens.decode("utf-8", errors="replace")
  return text
```
我们首先创建一个字典vocab，包含了256个原始token的字节表示，然后根据merges字典，将融合后的token pair的字节表示，赋值为原来的token pair的字节表示的拼接。最后我们可以使用decode函数，将一段tokens恢复为原来的文本。这样就不用一步一步去替换新的token idx为原来的token pair，而是直接将所有的token idx对应的字节表示拼接起来，再解码为文本。
## Encoding
和解码相对的就是我们怎么编码文本为tokens。编码的过程和解码的过程是相反的，我们从文本开始，将其编码为utf-8的字节序列，并转为list对象，此刻就成为了整数表示形式，然后我们反复更新这个tokens序列也就是编码它，除非它的长度小于2，即它无法再融合，或者最小的token pair都已经不在merges字典中了，就停止编码并返回此刻的tokens。为什么不从最大的pair对开始融合呢，因为最大的pair对也是从较小的融合而来的，在不融合出它的子集之前，在tokens中找不到找这个较大的pair对。
```python
def encode(text):
  # given a string, return list of integers (the tokens)
  tokens = list(text.encode("utf-8"))
  while len(tokens) >= 2:
    stats = get_stats(tokens)
    pair = min(stats, key=lambda p: merges.get(p, float("inf")))
    if pair not in merges:
      break # nothing else can be merged
    idx = merges[pair]
    tokens = merge(tokens, pair, idx)
  return tokens

print(encode(""))
```
所以，merges这个变量记录了所有的融合操作，可以用于编码阶段，而vocab变量记录了所有的token的字节表示，可以用于解码阶段。
## 正则表达式
有了编解码，我们就可以利用tokenizer，将文本转换为tokens，然后输入到LLM中进行处理，再从LLM中输出tokens，最后利用tokenizer将tokens转换为文本。
但是，训练这样一个简单的tokenizer无法解决一些特殊的问题，比如在英文句子中，单词之间有空格，而tokenizer是基于字节级的，所以空格会被编码为一个token，在训练集中就会出现很多这样的模式，例如`dog`这个单词，在文本中会出现许多代表相同意思的但是稍微有些不同字节表示的形式`'dog.',' dog','dog!','dog?'`，所以BPE会对每一个这样的不同模式的`dog`构建一个token，获得了只是稍有不同的`dog`这样的token，就是BPE把一些不该编码的部分也加入了进来，比如单词和标点符号。
所以必须要有一种人工的干预，强制不让一些符合规则的字符合并在一起。所以基本上我们可以构建这样的一个正则表达式`"""'s|'t|'re|'ve|'m|'ll|'d| ?\p{L}+| ?\p{N}+| ?[^\s\p{L}\p{N}]+|\s+(?!\S)|\s+"""`，用来分离那些不该被合并的字符。它的实现逻辑就是我们不去找那些烦扰的字符，而是去匹配我们合理的关心的模式，把这些模式提取出来并形成一个列表，那么没有提取出来的就是我们不关心的东西，所以我们只提取出了关切的部分。在`hello world how are your`这个例子中，经过正则匹配我们可以得到一个列表`['hello',' world',' how',' are',' you']`，我们在训练tokenizer之前，首先做的就是人工的划分单词，就是`' are' ' you'`中`e`不会和` you`中的第一个空格融合。接下来所有的融合都只会各自发生在列表中的各个元素中，不会跨元素进行融合，融合之后的结果再进行拼接就得到了我们训练好的tokenizer。
```python
for i in range(1, 101):
    if i % 3 == 0 and i % 5 == 0:
        print("FizzBuzz")
    elif i % 3 == 0:
        print("Fizz")
    elif i % 5 == 0:
        print("Buzz")
    else:
        print(i)
['\n', 'for', ' i', ' in', ' range', '(', '1', ',', ' 101', '):', '\n   ', ' if', ' i', ' %', ' 3', ' ==', ' 0', ' and', ' i', ' %', ' 5', ' ==', ' 0', ':', '\n       ', ' print', '("', 'FizzBuzz', '")', '\n   ', ' elif', ' i', ' %', ' 3', ' ==', ' 0', ':', '\n       ', ' print', '("', 'Fizz', '")', '\n   ', ' elif', ' i', ' %', ' 5', ' ==', ' 0', ':', '\n       ', ' print', '("', 'Buzz', '")', '\n   ', ' else', ':', '\n       ', ' print', '(', 'i', ')', '\n']
```
[tiktokenizer](https://tiktokenizer.vercel.app)这个网站中可以查看大语言模型的tokenizer结果,在GPT-2中，这些空格都没有进行合并，所以GPT-2中的tokenizer训练不仅仅是简单地将BPE应用到每一个提取的单词中，而是额外添加了一些规则，不过在这里我们理解预处理的过程，使用简单的处理方式就可以了。
![Token](/assets/images/2025/token.png)
在GPT-4o中，可以看到同样的代码，但是在tokenizer中，空格被合并了，对tokenizer的训练进行了优化。
![gpt-4o tokenizer](/assets/images/2025/gpt-4o tokenizer.png)
tiktoken库是OpenAI官方的一个tokenizer库，它可以用来将文本转换为tokens，也可以将tokens转换为文本。它的使用方法和我们之前实现的tokenizer类似，但是它是基于OpenAI的模型训练的，所以它的tokenizer结果和OpenAI的模型训练结果是一致的。
## special tokens
len(encoder) #256 raw  byte tokens. 5,000 merges. +1 special token。这个special token就是`<|endoftext|>`，它在OpenAI的模型中被用来表示一段文本的结束，也就是说这个特殊token前后的两段文本是独立的，它们没有任何关系。假设我们有一个很大的数据集，这些数据集都是独立的文本，从各个数据源获取的，我们当然希望这些文本之间不应该有语义上的关联，比如A文档是在叙述一段小说，而B文档则是关于科学的东西，所以需要这个标志告诉模型前后之间不相关，上一段的文档内容提供的信息不应该继承到下一段文本中来。当然，可以注册许多特殊的token，比如用来区分用户和系统提示信息，不希望将系统提示信息暴露给用户方。
## 实现正则化版本的tokenizer
这段代码就是一个正则化版本的tokenizer训练过程，它的基本用到的函数都没有改变，只是现在需要作用到每一个提取的单词中，而不是直接作用到整个文本中。获取每个独立单词中出现的所有字符对的统计信息，然后根据统计信息对每个单词进行合并操作，直到达到指定的合并次数。
```python
text_chunks=re.findall(self.compiled_pattern,text)
ids=[list(ch.encode('utf-8')) for ch in text_chunks]

for i in range(num_merges):
  idx=i+256
  stats={}
  for ch in ids:
    stats=self._get_stats(ch,stats)
  pair=max(stats,key=stats.get)
  ids=[self._merge(ch_ids,pair,idx) for ch_ids in ids]
  self.merges[pair]=idx
  first,second=pair
  self.vocab[idx]=self.vocab[first]+self.vocab[second]
```
## sentencepiece
sentencepiece是Google提出的一个tokenizer库，它是作用在码点上的。要理解码点，先要分清楚utf-8和unicode的区别。简单来说，unicode是一个字符集，它定义了每个字符的唯一编码号，这个编码号被称为码点，比如中文里面的`我`的码点就是`U+6211`，而utf-8是一个编码方案，计算机只能存储0和1，为了将码点存储下来，使用utf-8编码方案，`U+6211`的utf-8编码就是`0xE6 0x88 0x91`这3个字节，它将每个码点映射为一个或多个字节序列。sentencepiece和tiktoken的区别简而言之，就是它们使用BPE的层级不同，前者在unicode的码点下进行合并，而后者在更贴近计算机底层的utf-8层级使用。一个更贴近人类语言，一个更贴近计算机语言。它们面向的场景不同，tiktoken面向通用文本，跨语言，因为所有语言底层逻辑都是用二进制存储，也就是可以用utf-8表示。而sentencepiece面向多语言精细化分词，字符是人类语言的基本表意单元，从字符出发合并更符合语言的语义结构。
## vocab_size
在transformer中，vocab_size是指模型中使用的词汇表大小，它包括了所有可能的输入和输出符号。它会出现在两个地方，一个是token_embedding_table(shape=(vocab_size, d_model))，这个层会将每个token映射为一个d_model维的向量，所以vocab_size就是token_embedding_table的第一维。除此之外还有一个是在transformer的末端LM_head层，会将d_model维的向量映射为vocab_size维的向量，所以LM_head的输出维度就是vocab_size。就是我们会为每一个token(vocab_size大小)在每一个时间步上生成一个预测概率，随着我们有着越来越多的token，就需要预测更多的概率，就是在最后一个线性层上进行更多的点积运算。
所以token_embedding_table会随着vocab_size的增加而增加，LM_head线性层会随着vocab_size的增加而增加，会有更多的计算；更多的token意味着有更多的参数，可能担心很多参数没有得到充分的训练，因为引入更多的token，就是均摊了其它token在数据集上出现的频率，总体上所有token都会出现更少的示例占比，所以token的频率降低可能意味着它们在前向后向传播过程中的机会不多；此外，更多的token意味着对数据集的压缩更大，在适当的情况下我们可以使用较少的token来表达更多的文本(比如原来用80个token来表示一段话，现在只需要用50个token)，但是如果vocab_size过多，也可能会导致一大段话被压缩成一个token，这样模型在思考处理一定量的字符时，时间就不会那么充裕，因为过多的信息被过多压缩了。

# Part 9-Reproduce GPT-2(124M)
考虑到项目代码比较长，在这里详细解释各个部分，不如直接深入源码。在这里只是提炼其中的核心并分析，更细节的部分可以直接参考源码，源码中会有详细注释。
## 最初的样子
项目最初有CausalSelfAttention、MLP、Block、GPTConfig、GPT一共5个类型。
1. CausalSelfAttention
   key,query和value以一行代码的批量计算紧凑形式组织在了一起，使用register_buffer，表示生成的下三角矩阵张量是缓冲器即非可学习参数，会随模型保存，但不参与梯度更新。在前向传播时，会调整各个维度的位置，只需要记住，参与计算的最后两个维度一定是[T，head_size]。这里为了加速注意力计算，使用了pytorch提供的接口函数`F.scaled_dot_product_attention`。
2. MLP
   提供的是注意力计算后的FFN步骤。不过这里使用了gelu激活函数，且使用`tanh`近似GELU加快训练
3. Blcok
   将前两个模块组装到一起，由于是自然语言，所以这里使用的是层归一化。
4. GPTConfig
   这个类使用python的dataclass进行注册，方便参数的管理
5. GPT
   这个类从名称上就可以看出，是GPT模型的实现。其中重要的是其可以加载OpenAI官方GPT-2预训练好的权重参数。加载预训练权重时，需要获取二者的参数字典，即自定义实现的模型的参数字典和OpenAI官方GPT-2模型的参数字典。此外，OpenAI使用Conv1D,它的输入和输出的维度与普通的Linear层是相反的，所以需要转置后才能copy到自定义的模型中。
## 实现forward,autoregressive
   前向传播分为三个部分，将输入idx(shape=(B,T))进行token_embedding，得到(shape=(B,T,n_embd))和位置编码(shape=(T,n_embd))，将这两个张量相加。以block为组计算transformer结构，最后使用层归一化和线性层得到每一个待预测的token的logits(shape=(B,T,vocab_size))。
   实现自回归的生成器，首先使用tiktoken的gpt-2的tokenizer对输入的文本进行编码，之后就可以传入模型得到logits,套路和之前的第7部分的生成器内容是差不多的，不过这里是从前50最大概率中选取一个token作为下一个token。
## 实现损失计算
  在forward中添加了损失计算部分，如果传入的target不为空，则计算损失。
## 实现一个数据加载器
  要训练的数据从哪里来呢？怎么组织好直接可以传入模型进行训练呢？这就需要一个数据加载器帮助我们完成这些工作。DataLoader不仅会加载数据到内存中缓存好，还可以分批次扔出数据用于当前批次数据作为输入进入模型，进行训练，它通过一个`current_position`记录当前应该是第几个批次的数据，之后通过`next_batch`去获取对应位置的数据然后传递给模型进行训练。不过该数据加载中存在一个未解决的小bug,就是最后一段数据不足以支撑B*T批次大小时，会重置当前位置为0，意味着末尾有一段数据永远不会用于训练。保持这样处理的好处是，每个批次的数据都是固定的，内部的统计量不会改变，如果采用循环读取操作（当末尾的数据填充不满B*T时，使用头部的一段数据进行填充）会导致每个epoch训练时，批次的数据其统计量变化，可能（我猜测）会影响模型训练效果，此外，不利用末尾这一小段数据，在整个样本比例中其实占比不大，影响可以忽略。
  之后简单地使用一个优化器，训练模型，更新权重。
## 权重共享
权重共享有两个问题，一个是理解为什么要这样做？第二个就是分清权重矩阵的存储逻辑和运算逻辑。
先来看第一个问题，为什么要权重共享？
1. 降低参数量，这个是最直接的收益。参数的收益量是vocab_size*num_embd。
2. 符合自回归预测的任务逻辑
  语言模型的核心任务是子回归预测，给定前序token，预测下一个token。过程的本质是‘嵌入-编码-解码’的闭环过程，权重共享让这个闭环更合理。如果不共享权重，lm_head会学习另外一套逆映射，lm_head的本质是在学习一套wte映射的逆操作，将经过嵌入并编码后的结果映射回token。使用共享权重，就像是用同一把钥匙锁门和开门。
3. 词嵌入层wte的权重矩阵中，每一行对应一个token的词嵌入向量，行与行之间的距离(比如欧式距离)体现了token的语义相似度，当lm_head共享这套权重时，本质上就是`每个token的预测得分=隐藏向量*对应token的嵌入向量`，点积越大，说明二者的语义越匹配，预测概率越高，这套共享机制让预测过程直接利用了嵌入层学到的语义信息，确保语义相似的token在预测时会得到更高的关联得分，让模型的预测更符合语义逻辑。本质上是因为我们对嵌入层的嵌入向量之间的相似性解释，在预测时也希望能够利用上这种相似性，从而体现语义上的连贯性。
4. 此外，无偏设计（没有偏置）是保证了不会破坏语义上的对称。
权重矩阵的运算逻辑和存储逻辑是相反的
  pytorch中，变量x和权重矩阵w相乘时，其实是`x@W.T`，也就是说构建权重矩阵时我们是按照运算的逻辑构建的即权重矩阵的shape=[input,output]，而存储逻辑的形状其实是[output,input]，所以对于wte权重矩阵，因为在由token转到词嵌入空间中我们没有使用投影运算（矩阵相乘），而是直接使用索引，索引到当前token整数对应的wte的行，取出该行的词嵌入向量，所以wte通过`nn.Embedding`构建，而lm_head需要对编码后的向量进行投影，所以需要使用矩阵运算，构建时使用`nn.Linear`，这样，它构建时需要使用[input,output]这样的形式，但是底层存储时的形状会是[output,input]，转置一下就符合矩阵运算需要的形状了。
## 初始化权重
为了最真实地贯彻GPT-2的路线，初始化的设置也选择了最贴近GPT-2的初始化设置。
## 控制残差流动引起的方差变化
```python
x=torch.zero(768)
for i in range(100):
  x+=torch.randn(768)
```
通过以上的例子，模拟了一个残差流不断累加的过程，其方差会不断增加。为了控制方差，维持前后方差的一致性，需要对残差流动进行归一化处理。所以在每个transformer block中，有两次残差连接，所以针对残差连接处的计算，在初始化投影矩阵权重时，需要除以根号下的两倍总的transformer block数。
不过，这里有一个问题，就是这种方式确保的是最终输出的变量，其方差是不变的，但是在每个transformer block中，其方差都缩小了。就是这样一个意思，原本我们为了确保初始化时，矩阵乘法后，输出的方差是基本不变的，所以除以了根号下的输入维度数，比如768，这样每一层的输出其方差都维持在这个位置。但是呢，由于引入了残差连接，所以确保在最终输出的方差不变的情况下，又对残差处进一步除以了一个值，这就导致每一层其方差变小了。不再是原来的输出后其方差都维持在这个位置的说法了。
## 启用TF32训练
TF32是GPU内部的一种计算方式，默认情况下使用FP32，当激活了TF32后，在GPU内部计算矩阵乘法时(其它运算依然使用FP32)，会使用TF32进行计算，从而提高计算效率。输入输出依然是FP32，只是在计算时采用了TF32。矩阵乘法用 TF32 加速（FP32 输入→TF32 计算→FP32 输出），其他运算仍为 FP32。
## 启用BF16
有必要简单地区分下FP32、TF32、FP16和BF16，FP32有8位指数位，用来表示范围，23位精度位用来确保精度，TF32相对于FP32，在精度位上只使用了10位。FP16，在精度位上和TF32保持一致，都使用10位，但是它的指数位只有5位，而BF16，它的指数为和TF32一样，有8位，但是精度位进一步缩减，变为了7位。它的最佳实践文档可以参考[AMP](https://docs.pytorch.org/tutorials/recipes/recipes/amp_recipe.html)。基本上来说，就是在中间变量，将FP32转为BF16，进行训练和反向传播，梯度更新时再转回FP32确保精度。所以结合TF32(仅仅针对矩阵乘法)，在没有启用BF16时，它的输入输出是FP32，仅仅是中间计算时用了TF32加速，现在输入输出变成了BF16。所以BF16是整体上将FP32迁移成了BF16，不仅仅包括矩阵乘法，而是其所有的中间变量的临时存储都是BF16。总结起来就是核心是计算用BF16，参数存储用FP32。所以这就是为什么叫做混合精度的原因，一些东西在pytroch中仍然保持FP32(权重矩阵)，而一些东西精度降低了，成了BF16(激活值、中间计算的临时变量等等)。但是，又来了，又是但是，仔细查看上面的最佳实践文档可以知道，其实也并不是所有层都转成了BF16，不过总而言之，言而总之，启用BF16结合TF32，确实加速了我们的训练，节省了内存。
## 启用torch.compile
torch.compile对于加速的作用主要来自于减少python的开销和GPU读取次数。减少python的开销，简单地比喻就是python解释器就像厨师做饭，按照菜谱，要一步一步做，而自动化厨房只需要把原材料给它，就会自动运行。对于减少python的开销，torch.compile可以看到你所要操作的整个流程，而python解释器是逐行运行，它并不知道接下来会发生什么。torch.compile不会以一种`eager`模式运行，会优化运行的过程。它会首先移除python解释器在前向传播中的作用，将整个神经网络编译成不涉及python解释器的单一对象，然后直接运行。而对于GPU读取次数，举个简单例子就是说进行各种乘除法运算时，如果针对的同一个变量(该变量需要经过多种复合运算得到)，在没有使用torch.compile时，GPU会一步一步地在内核中运算-存储到GPU内存中-读取GPU内存中的内容-再次运算，而使用了torch.compile后，它就像知道了所有这些操作的最终结果都是为了得到那一个变量，就会直接在内核中进行连续的操作运算，而不用反复地读取存储了，这就是GPU读取次数的意思。
## 转换到Flash Attentino
简而言之，pytorch内部实现了这个机制，使得计算注意力分数的速度有了提升。
## fit nice number
计算机科学中，由于二进制的原因，许多对于数字的设定都偏爱2的次幂，这些神奇的数字。所以对于一些设定的参数，比如批次、时间步长等等，优化成2的倍数，能发挥更好的计算机性能。
## 梯度裁剪
就是防止出现梯度爆炸、梯度更新反复横跳等现象。在反向传播和梯度更新之间执行，一般常用的是收集所有参数的梯度计算其L2范数，之后设定阈值进行裁剪。
## 余弦衰减学习率调度器
需要注意，学习率调度器和优化器是两个不同的东西，一个是针对学习率，一个是在学习率固定下来后怎么去计算梯度更新的策略。在优化器的参数中，去更新刷新后的学习率。余弦学习率调度器本质上就是根据当前的训练epoch次数，去计算应该使用什么大小的学习率。
![cos-learning](/assets/images/2025/cos-learning.png)
## 权重衰减和梯度累积
  在GPT-2，GPT-3训练过程中采用了变化的batch size大小的策略进行训练。基于这样的观察和解释，在模型训练的初期，基本上是在学习忽略那些不常出现在训练集中的token，学习非常简单的偏差和类似的东西，每一个样本都在告诉模型，使用这些token，不使用那些Token,来自每一个样本的梯度实际上是高度相关的，在优化的初始阶段，它们看起来都大致相同，因为它们都在告诉模型这些token出现，那些token不出现。所以在训练初期，没有必要使用很大的batch size。只有当跨越过初期阶段，使用大批量样本才会展现出统计上的意义，去学习更深层次的东西，比如“语境歧义”（如 “苹果” 是水果还是公司）。怎么理解这段话呢？
简而言之，打个比方把模型训练比作 “老师教学生学语文”，batch size 比作 “一次布置的作业量”：
初期（学拼音、常用字）：学生的核心任务是 “记住常用字怎么写、怎么读”—— 所有作业（样本）都在重复 “听写常用字”，学生的错误（梯度）都集中在 “生僻字不会写”“常用字写错笔画” 上（梯度高度相关）。这时候布置 10 道题（小 batch）和 100 道题（大 batch）的效果一样 —— 学生都是在纠正相同的错误，100 道题只会让学生更累（浪费时间），不会更快掌握。
后期（学阅读理解、写作）：学生的任务是 “理解语境、掌握多义词、组织逻辑”—— 作业题（样本）五花八门：有的考 “‘打’在‘打球’和‘打电话’中的不同含义”，有的考 “议论文的论点论据”，有的考 “散文的情感表达”（梯度多样性高）。这时候布置 100 道题（大 batch）比 10 道题（小 batch）效果好 —— 学生能接触更多场景，避免 “只懂一道题，换题就错”（降低梯度噪声），学到的规律更全面（统计上的通用逻辑）。
所以，训练初期的核心是 “学简单规律”，梯度同质化，小 batch 足够用，且效率更高；
训练后期的核心是 “学复杂规律”，梯度异质化，大 batch 能覆盖更全的样本分布，让模型学到统计上的通用规律；
但在本项目的实现中，跳过了这个步骤，因为它会把问题复杂化，比如怎么动态地处理batch size带来的数据变化。
真实地遵循GPT-3的训练策略，其中提到了使用0.1的权重衰减，就是L2正则化。但是不是对所有的参数都进行正则化，主要是针对矩阵乘法部分的参数，比如线性层的权重矩阵。为此需要构建一个`configure_optimizer`函数，来确哪些需要权重衰减，哪些不需要。其中优化器使用到了`fused`这个选项，就是把许多计算需要用到许多核的情况，融合成了一个核，简而言之就是加快计算速度。
  在有限的资源，比如一个GPU中，如何使用0.5M大小的总token数(=B*T)进行训练呢？如果直接指定对应的batch size，那么GPU的内存会溢出。这个时候就要使用梯度累积的策略了。它允许我们采用串行的方式，模拟任何批次大小的数据。代价就是运行时间变成，处理多个序列然后把这些梯度加起来。所以基本策略就是使用一个小batch size，多次前向传播-反向传播，但是不更新梯度，直到重复`B/min_batch_size`次，再更新梯度。需要注意的是，我们使用了loss_accum来记录最终的0.5M这个batch size大小下的损失，它是被detach掉的。
```python
for micro_step in range(grad_accum_steps):
        x,y=train_loader.next_batch()
        x,y=x.to(device),y.to(device)
        with torch.autocast(device_type=device,dtype=torch.bfloat16):
            logits,loss=model(x,y)
        loss=loss/grad_accum_steps
        loss_accum+=loss.detach()
        loss.backward()
```
## Distributeddataparalle(DDP)
怎么利用多GPU进行训练。这里的难点在于我们需要想像有8个并行运行的程序在运行相同的代码，它们的区别就是`ddp_rank`。因为我们有了多GPU，原本代码中的一些参数值就得考虑到平均后的正确数值是多少了，比如原本我们在单个GPU中使用mini_batch，需要前向-反向传播`B/mini_batch`次才能进行一次梯度更新，现在考虑到使用多GPU，这个前向-反向传播次数进一步被平均了，所以需要`B/(mini_batch*num_GPU)次就可以了`。我们需要让`DataLoader`根据不同的GPU加载属于自己的那份数据，而不是每个GPU都加载同一段的数据。

目前总结下代码中的一个容易误导的地方，就是现在是按照总的优化器更新次数来训练模型，而不是按照epoch次数训练模型。按照传统epoch次数的理解，举例100个epoch，就需要每个epoch都要完整地遍历一次训练数据集。而现在，采用的是总的优化器更新次数，每次都遍历一定量的token数，比如设定总的优化器更新次数为max_steps，那么它等同于`max_steps/(total_token/batch_size_token)`个epoch。