---
description: 发起 implementer 实现 + reviewer 审核的迭代循环，直到审核通过
---
按以下循环处理用户请求，直到 reviewer 返回 PASS：

1. 用 Task 工具调用 subagent "implementer" 完成 {{input}} 的实现。
2. 用 Task 工具调用 subagent "reviewer" 审核 implementer 的改动，要求只读、输出结构化问题清单。
3. 若 reviewer 返回 PASS，把最终结论汇总给用户并结束。
4. 若 reviewer 列出了问题，用 task_id 续接 implementer 子会话，把问题清单交给它返工；回到第 2 步。
5. 超过 5 轮仍未通过时，把未解决的问题列出并询问用户是否继续。

注意：每轮返工必须使用 task_id 续接同一 implementer 会话以保留上下文；reviewer 同样使用 task_id 续接。
