#!/usr/bin/env python3
"""
LoRA蒸馏训练脚本
用DeepSeek-Coder-6.7B作为教师，蒸馏到TinyLlama-1.1B (添加LoRA)

使用方法:
    python train_lora_distill.py --output_dir ./lora-distilled

依赖安装:
    pip install transformers peft datasets accelerate bitsandbytes wandb
"""

import argparse
import os
import torch
import torch.nn.functional as F
from transformers import (
    AutoModelForCausalLM,
    AutoTokenizer,
    TrainingArguments,
    Trainer,
    DataCollatorForLanguageModeling,
)
from peft import LoraConfig, get_peft_model, prepare_model_for_kbit_training
from datasets import load_dataset
import wandb
from typing import Dict, Optional


class DistillationTrainer(Trainer):
    """自定义Trainer，添加知识蒸馏损失"""

    def __init__(self, teacher_model=None, temperature=2.0, alpha=0.7, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.teacher = teacher_model
        self.temperature = temperature
        self.alpha = alpha

        if self.teacher is not None:
            self.teacher.eval()
            # 冻结教师模型
            for param in self.teacher.parameters():
                param.requires_grad = False

    def compute_loss(self, model, inputs, return_outputs=False):
        """
        计算蒸馏损失 = α * KL散度 + (1-α) * 标准CE损失
        """
        # 学生模型前向传播
        outputs = model(**inputs)
        student_logits = outputs.logits

        # 标准语言模型损失
        labels = inputs.get("labels")
        if labels is None:
            labels = inputs["input_ids"]

        # Shift logits and labels for next token prediction
        shift_logits = student_logits[..., :-1, :].contiguous()
        shift_labels = labels[..., 1:].contiguous()

        ce_loss = F.cross_entropy(
            shift_logits.view(-1, shift_logits.size(-1)),
            shift_labels.view(-1),
            ignore_index=-100,
        )

        # 如果有教师模型，计算蒸馏损失
        if self.teacher is not None:
            with torch.no_grad():
                teacher_outputs = self.teacher(**inputs)
                teacher_logits = teacher_outputs.logits

            # Shift teacher logits
            teacher_logits = teacher_logits[..., :-1, :].contiguous()

            # 计算KL散度 (在温度缩放后的概率分布上)
            student_probs = F.log_softmax(shift_logits / self.temperature, dim=-1)
            teacher_probs = F.softmax(teacher_logits / self.temperature, dim=-1)

            distill_loss = F.kl_div(
                student_probs,
                teacher_probs,
                reduction="batchmean",
            ) * (self.temperature ** 2)

            # 推测式解码专用损失：鼓励top-1匹配
            teacher_top1 = teacher_logits.argmax(dim=-1)
            student_log_probs_at_teacher_top1 = torch.gather(
                student_probs,
                dim=-1,
                index=teacher_top1.unsqueeze(-1)
            ).squeeze(-1)
            spec_loss = -student_log_probs_at_teacher_top1.mean()

            # 总损失
            loss = (
                self.alpha * distill_loss +
                (1 - self.alpha) * ce_loss +
                0.1 * spec_loss  # 额外的推测式解码损失
            )

            # 记录各部分损失
            if self.state.global_step % 10 == 0:
                self.log({
                    "distill_loss": distill_loss.item(),
                    "ce_loss": ce_loss.item(),
                    "spec_loss": spec_loss.item(),
                    "total_loss": loss.item(),
                })
        else:
            loss = ce_loss

        return (loss, outputs) if return_outputs else loss


def setup_models(args):
    """加载并配置教师和学生模型"""

    print("=" * 60)
    print("📦 加载模型...")
    print("=" * 60)

    # 1. 加载Tokenizer
    tokenizer = AutoTokenizer.from_pretrained(args.teacher_model)
    if tokenizer.pad_token is None:
        tokenizer.pad_token = tokenizer.eos_token

    # 2. 加载教师模型 (只读，用于生成软标签)
    print(f"\n🎓 加载教师模型: {args.teacher_model}")
    # Use float16 for MPS compatibility (Apple Silicon doesn't support bfloat16)
    teacher = AutoModelForCausalLM.from_pretrained(
        args.teacher_model,
        torch_dtype=torch.float16,
        device_map="auto",
        low_cpu_mem_usage=True,
    )
    print(f"   教师参数量: {teacher.num_parameters() / 1e9:.2f}B")

    # 3. 加载学生模型
    print(f"\n👶 加载学生模型: {args.student_model}")
    student = AutoModelForCausalLM.from_pretrained(
        args.student_model,
        torch_dtype=torch.float16,
        device_map="auto",
        low_cpu_mem_usage=True,
    )
    print(f"   学生参数量: {student.num_parameters() / 1e9:.2f}B")

    # 4. 配置LoRA
    print("\n🔧 配置LoRA...")
    lora_config = LoraConfig(
        r=args.lora_r,
        lora_alpha=args.lora_alpha,
        target_modules=["q_proj", "v_proj", "k_proj", "o_proj"],
        lora_dropout=0.05,
        bias="none",
        task_type="CAUSAL_LM",
    )

    student = get_peft_model(student, lora_config)
    student.print_trainable_parameters()

    return teacher, student, tokenizer


def prepare_dataset(tokenizer, args):
    """准备训练数据集"""

    print("\n" + "=" * 60)
    print("📚 准备数据集...")
    print("=" * 60)

    if args.dataset == "codeparrot/github-code":
        # 小型代码数据集 (快速测试)
        print("使用 CodeParrot GitHub-Code 数据集")
        dataset = load_dataset(
            "codeparrot/github-code",
            languages=["Python"],
            split="train",
            streaming=True,
        )
        # 取前10k样本
        dataset = dataset.take(args.max_samples)

    elif args.dataset == "bigcode/the-stack":
        # 大型代码数据集 (生产级)
        print("使用 The Stack 数据集")
        dataset = load_dataset(
            "bigcode/the-stack-dedup",
            data_dir="data/python",
            split="train",
            streaming=True,
        )
        dataset = dataset.take(args.max_samples)

    else:
        # 自定义数据集
        print(f"使用自定义数据集: {args.dataset}")
        dataset = load_dataset(args.dataset, split="train")

    # Tokenize
    def tokenize_function(examples):
        # 处理不同数据集的字段名
        if "content" in examples:
            texts = examples["content"]
        elif "code" in examples:
            texts = examples["code"]
        elif "text" in examples:
            texts = examples["text"]
        else:
            raise ValueError("Unknown text field in dataset")

        return tokenizer(
            texts,
            truncation=True,
            max_length=args.max_length,
            padding="max_length",
        )

    print(f"正在tokenize数据... (max_length={args.max_length})")
    tokenized_dataset = dataset.map(
        tokenize_function,
        batched=True,
        remove_columns=dataset.column_names if hasattr(dataset, 'column_names') else [],
    )

    return tokenized_dataset


def main():
    parser = argparse.ArgumentParser(description="LoRA蒸馏训练")

    # 模型参数
    parser.add_argument(
        "--teacher_model",
        type=str,
        default="deepseek-ai/deepseek-coder-6.7b-instruct",
        help="教师模型路径"
    )
    parser.add_argument(
        "--student_model",
        type=str,
        default="TinyLlama/TinyLlama-1.1B-Chat-v1.0",
        help="学生模型路径"
    )

    # LoRA参数
    parser.add_argument("--lora_r", type=int, default=16, help="LoRA秩")
    parser.add_argument("--lora_alpha", type=int, default=32, help="LoRA alpha")

    # 蒸馏参数
    parser.add_argument("--temperature", type=float, default=2.0, help="蒸馏温度")
    parser.add_argument("--alpha", type=float, default=0.7, help="蒸馏损失权重")

    # 数据参数
    parser.add_argument(
        "--dataset",
        type=str,
        default="codeparrot/github-code",
        help="训练数据集"
    )
    parser.add_argument("--max_samples", type=int, default=10000, help="最大样本数")
    parser.add_argument("--max_length", type=int, default=512, help="最大序列长度")

    # 训练参数
    parser.add_argument("--output_dir", type=str, default="./lora-distilled", help="输出目录")
    parser.add_argument("--num_epochs", type=int, default=3, help="训练轮数")
    parser.add_argument("--batch_size", type=int, default=4, help="批大小")
    parser.add_argument("--gradient_accumulation_steps", type=int, default=4, help="梯度累积步数")
    parser.add_argument("--learning_rate", type=float, default=5e-5, help="学习率")
    parser.add_argument("--warmup_steps", type=int, default=100, help="预热步数")
    parser.add_argument("--logging_steps", type=int, default=10, help="日志记录间隔")
    parser.add_argument("--save_steps", type=int, default=500, help="保存间隔")

    # 其他
    parser.add_argument("--use_wandb", action="store_true", help="使用wandb记录")
    parser.add_argument("--wandb_project", type=str, default="deepseek-lora-distill", help="wandb项目名")

    args = parser.parse_args()

    # 初始化wandb
    if args.use_wandb:
        wandb.init(project=args.wandb_project, config=vars(args))

    # 加载模型
    teacher, student, tokenizer = setup_models(args)

    # 准备数据
    train_dataset = prepare_dataset(tokenizer, args)

    # 训练参数
    training_args = TrainingArguments(
        output_dir=args.output_dir,
        num_train_epochs=args.num_epochs,
        per_device_train_batch_size=args.batch_size,
        gradient_accumulation_steps=args.gradient_accumulation_steps,
        learning_rate=args.learning_rate,
        warmup_steps=args.warmup_steps,
        logging_steps=args.logging_steps,
        save_steps=args.save_steps,
        save_total_limit=3,
        fp16=True,  # Use fp16 for MPS compatibility
        bf16=False,  # BFloat16 not supported on MPS
        optim="adamw_torch",
        gradient_checkpointing=True,
        report_to="wandb" if args.use_wandb else "none",
        load_best_model_at_end=False,
        remove_unused_columns=False,
    )

    # 数据整理器
    data_collator = DataCollatorForLanguageModeling(
        tokenizer=tokenizer,
        mlm=False,
    )

    # 创建Trainer
    trainer = DistillationTrainer(
        teacher_model=teacher,
        temperature=args.temperature,
        alpha=args.alpha,
        model=student,
        args=training_args,
        train_dataset=train_dataset,
        data_collator=data_collator,
    )

    # 开始训练
    print("\n" + "=" * 60)
    print("🚀 开始训练...")
    print("=" * 60)
    print(f"总样本数: {args.max_samples}")
    print(f"Batch size: {args.batch_size}")
    print(f"梯度累积: {args.gradient_accumulation_steps}")
    print(f"有效batch: {args.batch_size * args.gradient_accumulation_steps}")
    print(f"训练轮数: {args.num_epochs}")
    print(f"蒸馏温度: {args.temperature}")
    print(f"蒸馏权重α: {args.alpha}")
    print("=" * 60 + "\n")

    trainer.train()

    # 保存模型
    print("\n💾 保存模型...")
    student.save_pretrained(args.output_dir)
    tokenizer.save_pretrained(args.output_dir)

    # 合并LoRA权重 (可选)
    print("\n🔗 合并LoRA权重...")
    merged_model = student.merge_and_unload()
    merged_model.save_pretrained(os.path.join(args.output_dir, "merged"))
    tokenizer.save_pretrained(os.path.join(args.output_dir, "merged"))

    print("\n✅ 训练完成!")
    print(f"   LoRA模型保存在: {args.output_dir}")
    print(f"   合并模型保存在: {args.output_dir}/merged")
    print("\n下一步:")
    print("   1. 转换为GGUF:")
    print(f"      python llama.cpp/convert_hf_to_gguf.py {args.output_dir}/merged")
    print("   2. 量化:")
    print(f"      ./llama.cpp/llama-quantize model.gguf model-q4.gguf Q4_K_M")
    print("   3. 测试推测式解码性能")

    if args.use_wandb:
        wandb.finish()


if __name__ == "__main__":
    main()
