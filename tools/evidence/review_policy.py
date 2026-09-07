"""按归档用户授权重算复核要求；不将 AI 复核表示为人工批准。"""
import json
import re
from pathlib import PurePosixPath
from tools.evidence.common import sha_bytes


def archived_json(raw):
    """归档 JSON 同样拒绝重复字段与非有限数，避免歧义解释。"""
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate JSON member: ' + key)
            result[key] = value
        return result
    def invalid(value):
        raise ValueError('nonfinite JSON number: ' + value)
    return json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)


def evaluate(spec, records, task_id, inputs_sha256, archived_read, source_inputs):
    """纯验证入口；调用方另行验证记录原始附件、自动结果及来源归档。

    archived_read(path) 必须读取已验证的归档字节，禁止替换为活工作区文件。
    source_inputs 是该归档的路径/摘要/长度清单。返回错误与复核是否齐全。
    """
    errors = []
    def require(condition, message):
        if not condition:
            errors.append(message)
    try:
        required = spec.get('review_required', ['human'])
        require(isinstance(required, list) and bool(required) and
                all(isinstance(kind, str) for kind in required) and
                len(required) == len(set(required)), 'required review set invalid')
        if errors:
            return errors, False
        if 'review_policy' not in spec:
            require('human' in required, 'legacy review policy requires human')
            approved = {record.get('review_kind', 'human') for record in records
                if record.get('task_id') == task_id and record.get('review_status') == 'Approved'
                and record.get('reviewed_inputs_sha256') == inputs_sha256 and record.get('approval_text')}
            return errors, not errors and set(required).issubset(approved)
        binding = spec['review_policy']
        require(isinstance(binding, dict) and set(binding) == {'format','mode','path','sha256'},
                'review policy binding fields invalid')
        if errors:
            return errors, False
        require(binding['format'] == 'ock.review-policy-binding/1' and binding['mode'] == 'ai-self-review',
                'review policy binding version or mode invalid')
        require(set(required) == {'spec','code'}, 'AI policy requires exactly spec and code')
        name = binding['path']
        require(isinstance(name, str) and bool(name) and '\\' not in name and ':' not in name
                and not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
                and PurePosixPath(name).as_posix() == name, 'review policy path must be canonical relative path')
        require(isinstance(binding['sha256'], str) and re.fullmatch(r'[0-9a-f]{64}', binding['sha256']) is not None,
                'review policy hash invalid')
        if errors:
            return errors, False
        rows = [row for row in source_inputs if row['path'] == name]
        require(len(rows) == 1, 'review policy missing or duplicated in source inputs')
        raw = archived_read(name)
        require(sha_bytes(raw) == binding['sha256'], 'archived review policy hash mismatch')
        if len(rows) == 1:
            require(rows[0]['sha256'] == binding['sha256'] and rows[0]['size'] == len(raw),
                    'review policy source fingerprint mismatch')
        policy = archived_json(raw)
        require(type(policy.get('version')) is int and policy['version'] == 1, 'archived review policy version unsupported')
        require(policy.get('format') == 'ock.review-policy/1' and policy.get('mode') == 'ai-self-review',
                'archived review policy version or mode invalid')
        require(policy.get('user_text') == '自我复核和自动验收，无需人工', 'explicit user authorization missing')
        require(policy.get('required_reviews') == ['spec','code'] and policy.get('review_actor') == 'AI'
                and policy.get('human_review_required') is False, 'archived review policy requirements invalid')
        require(not any(key.startswith('human') and key != 'human_review_required' for key in policy),
                'AI policy cannot claim human approval')
        approved = set()
        seen = set()
        for record in records:
            kind = record.get('review_kind')
            require(kind in ('spec','code') and kind not in seen, 'AI review kind invalid or duplicated')
            seen.add(kind)
            require(record.get('task_id') == task_id and record.get('reviewed_inputs_sha256') == inputs_sha256,
                    'AI review task or source mismatch')
            require(record.get('actor_type') == 'AI', 'AI review actor must be AI')
            require(isinstance(record.get('reviewer'), str) and bool(record['reviewer'].strip()), 'AI reviewer missing')
            require(not any(key.startswith('human') for key in record), 'AI review cannot claim human approval')
            status = record.get('review_status')
            require(status in ('Approved','Pending'), 'AI review status invalid or rejected')
            if status == 'Approved':
                require(isinstance(record.get('approval_text'), str) and bool(record['approval_text'].strip()),
                        'AI approval text missing')
                approved.add(kind)
        return errors, not errors and approved == {'spec','code'}
    except (KeyError, TypeError, ValueError, OSError, AttributeError) as exc:
        errors.append('invalid archived review policy or record: ' + str(exc))
        return errors, False
